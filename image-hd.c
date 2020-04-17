/*
 * Copyright (c) 2011 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *           (c) 2011 Michael Olbrich <m.olbrich@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <endian.h>

#include "genimage.h"

struct hdimage {
	cfg_bool_t partition_table;
	unsigned int extended_partition;
	unsigned long long align;
	unsigned long long extended_lba;
	uint32_t disksig;
	const char *disk_uuid;
	cfg_bool_t gpt;
	unsigned long long gpt_location;
	cfg_bool_t fill;
};

struct mbr_partition_entry {
	unsigned char boot;

	unsigned char first_chs[3];

	unsigned char partition_type;

	unsigned char last_chs[3];

	uint32_t relative_sectors;
	uint32_t total_sectors;
} __attribute__((packed));

struct gpt_header {
	unsigned char signature[8];
	uint32_t revision;
	uint32_t header_size;
	uint32_t header_crc;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	unsigned char disk_uuid[16];
	uint64_t starting_lba;
	uint32_t number_entries;
	uint32_t entry_size;
	uint32_t table_crc;
} __attribute__((packed));

struct gpt_partition_entry {
	unsigned char type_uuid[16];
	unsigned char uuid[16];
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t flags;
	uint16_t name[36];
} __attribute__((packed));

#define GPT_ENTRIES 		128
#define GPT_SECTORS		(1 + GPT_ENTRIES * sizeof(struct gpt_partition_entry) / 512)
#define GPT_REVISION_1_0	0x00010000

#define GPT_PE_FLAG_BOOTABLE	(1ULL << 2)
#define GPT_PE_FLAG_READ_ONLY	(1ULL << 60)
#define GPT_PE_FLAG_HIDDEN	(1ULL << 62)
#define GPT_PE_FLAG_NO_AUTO	(1ULL << 63)

static void hdimage_setup_chs(unsigned int lba, unsigned char *chs)
{
	const unsigned int hpc = 255;
	const unsigned int spt = 63;
	unsigned int s, c;

	chs[0] = (lba/spt)%hpc;
	c = (lba/(spt * hpc));
	s = (lba > 0) ?(lba%spt + 1) : 0;
	chs[1] = ((c & 0x300) >> 2) | (s & 0xff);
	chs[2] = (c & 0xff);
}

static int hdimage_insert_mbr(struct image *image, struct list_head *partitions, int hybrid)
{
	struct hdimage *hd = image->handler_priv;
	char mbr[6+4*sizeof(struct mbr_partition_entry)+2], *part_table;
	struct partition *part;
	int ret, i = 0;

	if (hybrid) {
		image_info(image, "writing hybrid MBR\n");
	} else {
		image_info(image, "writing MBR\n");
	}

	memset(mbr, 0, sizeof(mbr));
	memcpy(mbr, &hd->disksig, sizeof(hd->disksig));
	part_table = mbr + 6;

	list_for_each_entry(part, partitions, list) {
		struct mbr_partition_entry *entry;

		if (!part->in_partition_table)
			continue;

		if (hybrid && !part->partition_type)
			continue;

		if (hybrid && part->extended)
			continue;

		entry = (struct mbr_partition_entry *)(part_table + i *
				sizeof(struct mbr_partition_entry));

		entry->boot = part->bootable ? 0x80 : 0x00;
		if (!part->extended) {
			entry->partition_type = part->partition_type;
			entry->relative_sectors = part->offset/512;
			entry->total_sectors = part->size/512;
		}
		else {
			unsigned long long size = 0;
			struct partition *p = part;
			list_for_each_entry_from(p, partitions, list) {
				if (!p->extended)
					break;
				size += hd->align + p->size;
			}
			entry->partition_type = 0x0F;
			entry->relative_sectors = (part->offset - hd->align)/512;
			entry->total_sectors = size/512;
		}
		hdimage_setup_chs(entry->relative_sectors, entry->first_chs);
		hdimage_setup_chs(entry->relative_sectors +
				entry->total_sectors - 1, entry->last_chs);

		if (part->extended)
			break;
		i++;
	}

	if (hybrid) {
		struct mbr_partition_entry *entry;

		entry = (struct mbr_partition_entry *)(part_table + i *
			sizeof(struct mbr_partition_entry));

		entry->boot = 0x00;

		entry->partition_type = 0xee;
		entry->relative_sectors = 1;
		entry->total_sectors = hd->gpt_location / 512 + GPT_SECTORS - 2;

		hdimage_setup_chs(entry->relative_sectors, entry->first_chs);
		hdimage_setup_chs(entry->relative_sectors +
		entry->total_sectors - 1, entry->last_chs);
	}

	part_table += 4 * sizeof(struct mbr_partition_entry);
	part_table[0] = 0x55;
	part_table[1] = 0xaa;

	ret = insert_data(image, mbr, imageoutfile(image), sizeof(mbr), 440);
	if (ret) {
		if (hybrid) {
			image_error(image, "failed to write hybrid MBR\n");
		} else {
			image_error(image, "failed to write MBR\n");
		}
		return ret;
	}

	return 0;
}

static int hdimage_insert_ebr(struct image *image, struct partition *part)
{
	struct hdimage *hd = image->handler_priv;
	struct mbr_partition_entry *entry;
	char ebr[4*sizeof(struct mbr_partition_entry)+2], *part_table;
	int ret;

	image_info(image, "writing EBR\n");

	memset(ebr, 0, sizeof(ebr));
	part_table = ebr;
	entry = (struct mbr_partition_entry *)part_table;

	entry->boot = 0x00;
	entry->partition_type = part->partition_type;
	entry->relative_sectors = hd->align/512;
	entry->total_sectors = part->size/512;
	hdimage_setup_chs(entry->relative_sectors, entry->first_chs);
	hdimage_setup_chs(entry->relative_sectors +
			entry->total_sectors - 1, entry->last_chs);
	struct partition *p = part;
	list_for_each_entry_continue(p, &image->partitions, list) {
		++entry;
		entry->boot = 0x00;
		entry->partition_type = 0x0F;
		entry->relative_sectors = (p->offset - hd->align - hd->extended_lba)/512;
		entry->total_sectors = (p->size + hd->align)/512;
		hdimage_setup_chs(entry->relative_sectors, entry->first_chs);
		hdimage_setup_chs(entry->relative_sectors +
				entry->total_sectors - 1, entry->last_chs);
		break;
	}

	part_table += 4 * sizeof(struct mbr_partition_entry);
	part_table[0] = 0x55;
	part_table[1] = 0xaa;

	ret = insert_data(image, ebr, imageoutfile(image), sizeof(ebr),
			  part->offset - hd->align + 446);
	if (ret) {
		image_error(image, "failed to write EBR\n");
		return ret;
	}

	return 0;
}

static const char *
gpt_partition_type_lookup(char shortcut)
{
	switch(shortcut) {
	case 'L': return "0fc63daf-8483-4772-8e79-3d69d8477de4";
	case 'S': return "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f";
	case 'H': return "933ac7e1-2eb4-4f13-b844-0e14e2aef915";
	case 'U': return "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
	case 'R': return "a19d880f-05fc-4d3b-a006-743f0f84911e";
	case 'V': return "e6d6d379-f507-44c2-a23c-238f2a3df928";
	case 'F': return "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7";
	}
	return NULL;
}


static int hdimage_insert_protective_mbr(struct image *image)
{
	struct partition mbr;
	struct list_head mbr_list = LIST_HEAD_INIT(mbr_list);
	int ret = 0;

	image_info(image, "writing protective MBR\n");

	memset(&mbr, 0, sizeof(struct partition));
	mbr.offset = 512;
	mbr.size = image->size - 512;
	mbr.in_partition_table = 1;
	mbr.partition_type = 0xee;
	list_add_tail(&mbr.list, &mbr_list);
	ret = hdimage_insert_mbr(image, &mbr_list, 0);
	if (ret) {
		image_error(image,"failed to write protective MBR\n");
		return ret;
	}

	return 0;
}

static int hdimage_insert_gpt(struct image *image, struct list_head *partitions)
{
	struct hdimage *hd = image->handler_priv;
	const char *outfile = imageoutfile(image);
	struct gpt_header header;
	struct gpt_partition_entry table[GPT_ENTRIES];
	struct partition *part;
	unsigned i, j;
	int hybrid, ret;

	image_info(image, "writing GPT\n");

	memset(&header, 0, sizeof(struct gpt_header));
	memcpy(header.signature, "EFI PART", 8);
	header.revision = htole32(GPT_REVISION_1_0);
	header.header_size = htole32(sizeof(struct gpt_header));
	header.current_lba = htole64(1);
	header.backup_lba = htole64(image->size/512 - 1);
	header.last_usable_lba = htole64(image->size/512 - 1 - GPT_SECTORS);
	uuid_parse(hd->disk_uuid, header.disk_uuid);
	header.starting_lba = htole64(hd->gpt_location/512);
	header.number_entries = htole32(GPT_ENTRIES);
	header.entry_size = htole32(sizeof(struct gpt_partition_entry));

	hybrid = 0;
	i = 0;
	memset(&table, 0, sizeof(table));
	list_for_each_entry(part, partitions, list) {
		if (header.first_usable_lba == 0 && part->in_partition_table)
			header.first_usable_lba = htole64(part->offset / 512);

		if (!part->in_partition_table)
			continue;

		uuid_parse(part->partition_type_uuid, table[i].type_uuid);
		uuid_parse(part->partition_uuid, table[i].uuid);
		table[i].first_lba = htole64(part->offset/512);
		table[i].last_lba = htole64((part->offset + part->size)/512 - 1);
		table[i].flags =
			(part->bootable ? GPT_PE_FLAG_BOOTABLE : 0) |
			(part->read_only ? GPT_PE_FLAG_READ_ONLY : 0) |
			(part->hidden ? GPT_PE_FLAG_HIDDEN : 0) |
			(part->no_automount ? GPT_PE_FLAG_NO_AUTO : 0);
		for (j = 0; j < strlen(part->name) && j < 36; j++)
			table[i].name[j] = htole16(part->name[j]);

		if (part->partition_type)
			hybrid++;

		i++;
	}

	if (hybrid > 3) {
		image_error(image, "hybrid MBR partitions (%i) exceeds maximum of 3\n",
			    hybrid);
		return -EINVAL;
	}

	header.table_crc = htole32(crc32(table, sizeof(table)));

	header.header_crc = htole32(crc32(&header, sizeof(header)));
	ret = insert_data(image, (char *)&header, outfile, sizeof(header), 512);
	if (ret) {
		image_error(image, "failed to write GPT\n");
		return ret;
	}
	ret = insert_data(image, (char *)&table, outfile, sizeof(table), hd->gpt_location);
	if (ret) {
		image_error(image, "failed to write GPT table\n");
		return ret;
	}

	ret = pad_file(image, NULL, image->size, 0x0, MODE_APPEND);
	if (ret) {
		image_error(image, "failed to pad image to size %lld\n",
			    part->offset);
		return ret;
	}

	header.header_crc = 0;
	header.current_lba = htole64(image->size/512 - 1);
	header.backup_lba = htole64(1);
	header.starting_lba = htole64(image->size/512 - GPT_SECTORS);
	header.header_crc = htole32(crc32(&header, sizeof(header)));
	ret = insert_data(image, (char *)&table, outfile, sizeof(table),
			  image->size - GPT_SECTORS*512);
	if (ret) {
		image_error(image, "failed to write backup GPT table\n");
		return ret;
	}
	ret = insert_data(image, (char *)&header, outfile, sizeof(header),
			  image->size - 512);
	if (ret) {
		image_error(image, "failed to write backup GPT\n");
		return ret;
	}

	if (hybrid) {
		ret = hdimage_insert_mbr(image, partitions, hybrid);
	} else {
		ret = hdimage_insert_protective_mbr(image);
	}
	if (ret) {
		return ret;
	}

	return 0;
}


static int hdimage_generate(struct image *image)
{
	struct partition *part;
	struct hdimage *hd = image->handler_priv;
	enum pad_mode mode = MODE_OVERWRITE;
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		const char *infile;

		image_info(image, "adding partition '%s'%s%s%s%s ...\n", part->name,
			part->in_partition_table ? " (in MBR)" : "",
			part->image ? " from '": "",
			part->image ? part->image : "",
			part->image ? "'" : "");

		if (part->image || part->extended) {
			ret = pad_file(image, NULL, part->offset, 0x0, mode);
			if (ret) {
				image_error(image, "failed to pad image to size %lld\n",
						part->offset);
				return ret;
			}
			mode = MODE_APPEND;
		}

		if (part->extended) {
			ret = hdimage_insert_ebr(image, part);
			if (ret) {
				image_error(image, "failed to write EBR\n");
				return ret;
			}
		}

		if (!part->image)
			continue;

		child = image_get(part->image);
		infile = imageoutfile(child);

		ret = pad_file(image, infile, part->offset + child->size, 0x0, MODE_APPEND);

		if (ret) {
			image_error(image, "failed to write image partition '%s'\n",
					part->name);
			return ret;
		}
	}

	if (hd->fill) {
		ret = extend_file(image, image->size);
		if (ret) {
			image_error(image, "failed to fill the image.\n");
			return ret;
		}
	}

	if (hd->partition_table) {
		if (hd->gpt) {
			ret = hdimage_insert_gpt(image, &image->partitions);
			if (ret)
				return ret;
		}
		else {
			ret = hdimage_insert_mbr(image, &image->partitions, 0);
			if (ret)
				return ret;
		}
		return reload_partitions(image);
	}

	return 0;
}

static unsigned long long roundup(unsigned long long value, unsigned long long align)
{
	return ((value - 1)/align + 1) * align;
}

static int hdimage_setup(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	int has_extended, autoresize = 0;
	unsigned int partition_table_entries = 0;
	unsigned long long now = 0;
	const char *disk_signature;
	struct hdimage *hd = xzalloc(sizeof(*hd));

	hd->align = cfg_getint_suffix(cfg, "align");
	hd->partition_table = cfg_getbool(cfg, "partition-table");
	hd->extended_partition = cfg_getint(cfg, "extended-partition");
	disk_signature = cfg_getstr(cfg, "disk-signature");
	hd->gpt = cfg_getbool(cfg, "gpt");
	hd->gpt_location = cfg_getint_suffix(cfg, "gpt-location");
	hd->fill = cfg_getbool(cfg, "fill");
	hd->disk_uuid = cfg_getstr(cfg, "disk-uuid");

	if (hd->extended_partition > 4) {
		image_error(image, "invalid extended partition index (%i). must be "
				"inferior or equal to 4 (0 for automatic)\n",
				hd->extended_partition);
		return -EINVAL;
	}

	if ((hd->align % 512) || (hd->align == 0)) {
		image_error(image, "partition alignment (%lld) must be a "
				"multiple of 1 sector (512 bytes)\n", hd->align);
		return -EINVAL;
	}
	list_for_each_entry(part, &image->partitions, list) {
		if (part->in_partition_table)
			++partition_table_entries;
	}
	if (!hd->gpt && !hd->extended_partition && partition_table_entries > 4)
		hd->extended_partition = 4;
	has_extended = hd->extended_partition > 0;

	if (hd->disk_uuid ) {
		if (uuid_validate(hd->disk_uuid) == -1) {
			image_error(image, "invalid disk UUID: %s\n", hd->disk_uuid);
			return -EINVAL;
		}
	}
	else {
		hd->disk_uuid = uuid_random();
	}

	if (!strcmp(disk_signature, "random"))
		hd->disksig = random();
	else
		hd->disksig = strtoul(disk_signature, NULL, 0);

	if (hd->gpt_location == 0) {
		hd->gpt_location = 2*512;
	}
	else if (hd->gpt_location % 512) {
		image_error(image, "GPT table location (%lld) must be a "
				   "multiple of 1 sector (512 bytes)", hd->gpt_location);
	}

	partition_table_entries = 0;
	list_for_each_entry(part, &image->partitions, list) {
		if (autoresize) {
			image_error(image, "'autoresize' is only supported "
					"for the last partition\n");
			return -EINVAL;
		}
		autoresize = part->autoresize;
		if (autoresize && image->size == 0) {
			image_error(image, "the images size must be specified "
					"when using a 'autoresize' partition\n");
			return -EINVAL;
		}
		if (hd->gpt) {
			if (strlen(part->partition_type_uuid) == 1) {
				const char *uuid;
				uuid = gpt_partition_type_lookup(part->partition_type_uuid[0]);
				if (!uuid) {
					image_error(image,
						    "part %s has invalid type shortcut: %c\n",
						    part->name, part->partition_type_uuid[0]);
					return -EINVAL;
				}
				part->partition_type_uuid = uuid;
			}
			if (uuid_validate(part->partition_type_uuid) == -1) {
				image_error(image,
					    "part %s has invalid partition type UUID: %s\n",
					    part->name, part->partition_type_uuid);
				return -EINVAL;
			}
			if (part->partition_uuid) {
				if (uuid_validate(part->partition_uuid) == -1) {
					image_error(image,
						    "part %s has invalid partition UUID: %s\n",
						    part->name, part->partition_uuid);
					return -EINVAL;
				}
			}
			else {
				part->partition_uuid = uuid_random();
			}
		}
		/* reserve space for extended boot record if necessary */
		if (part->in_partition_table)
			++partition_table_entries;
		part->extended = has_extended &&
			(partition_table_entries >= hd->extended_partition);
		if (part->extended) {
			if (!hd->extended_lba)
				hd->extended_lba = now;
			now = roundup(now, hd->align);
			now += hd->align;
		}
		if (part->in_partition_table && (part->offset % hd->align)) {
			image_error(image, "part %s offset (%lld) must be a"
					"multiple of %lld bytes\n",
					part->name, part->offset, hd->align);
			return -EINVAL;
		}
		if (part->offset && part->in_partition_table) {
			if (now > part->offset) {
				image_error(image, "part %s overlaps with previous partition\n",
						part->name);
				return -EINVAL;
			}
		} else if (!part->offset && part->in_partition_table) {
			if (!now && hd->partition_table) {
				now = 512;
				if (hd->gpt)
					now = hd->gpt_location + (GPT_SECTORS - 1) * 512;
			}
			part->offset = roundup(now, hd->align);
		}
		if (autoresize) {
			long long partsize = image->size - now;
			if (hd->gpt)
				partsize -= GPT_SECTORS * 512;
			if (partsize < 0) {
				image_error(image, "partitions exceed device size\n");
				return -EINVAL;
			}
			part->size = partsize;
		}
		if (part->image) {
			struct image *child = image_get(part->image);
			if (!child) {
				image_error(image, "could not find %s\n",
						part->image);
				return -EINVAL;
			}
			if (!part->size) {
				if (part->in_partition_table)
					part->size = roundup(child->size, hd->align);
				else
					part->size = child->size;
			} else if (child->size > part->size) {
				image_error(image, "part %s size (%lld) too small for %s (%lld)\n",
						part->name, part->size, child->file, child->size);
				return -EINVAL;
			}
		}
		if (!part->size) {
			image_error(image, "part %s size must not be zero\n",
					part->name);
			return -EINVAL;
		}
		if (part->in_partition_table && (part->size % 512)) {
			image_error(image, "part %s size (%lld) must be a "
					"multiple of 1 sector (512 bytes)\n",
					part->name, part->size);
			return -EINVAL;
		}
		now = part->offset + part->size;
	}

	if (hd->gpt)
		now += GPT_SECTORS * 512;

	if (image->size > 0 && now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}

	if (image->size == 0)
		image->size = now;

	image->handler_priv = hd;

	return 0;
}

cfg_opt_t hdimage_opts[] = {
	CFG_STR("align", "512", CFGF_NONE),
	CFG_STR("disk-signature", "", CFGF_NONE),
	CFG_STR("disk-uuid", NULL, CFGF_NONE),
	CFG_BOOL("partition-table", cfg_true, CFGF_NONE),
	CFG_INT("extended-partition", 0, CFGF_NONE),
	CFG_BOOL("gpt", cfg_false, CFGF_NONE),
	CFG_STR("gpt-location", NULL, CFGF_NONE),
	CFG_BOOL("fill", cfg_false, CFGF_NONE),
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

