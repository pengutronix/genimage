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
#include <stdbool.h>

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
	cfg_bool_t gpt_no_backup;
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
ct_assert(sizeof(struct mbr_partition_entry) == 16);

struct mbr_tail {
	uint32_t disk_signature;
	uint16_t copy_protect;
	struct mbr_partition_entry part_entry[4];
	uint16_t boot_signature;
} __attribute__((packed));
ct_assert(sizeof(struct mbr_tail) == 72);

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
ct_assert(sizeof(struct gpt_header) == 92);

struct gpt_partition_entry {
	unsigned char type_uuid[16];
	unsigned char uuid[16];
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t flags;
	uint16_t name[36];
} __attribute__((packed));
ct_assert(sizeof(struct gpt_partition_entry) == 128);

#define GPT_ENTRIES 		128
#define GPT_SECTORS		(1 + GPT_ENTRIES * sizeof(struct gpt_partition_entry) / 512)
#define GPT_REVISION_1_0	0x00010000

#define GPT_PE_FLAG_BOOTABLE	(1ULL << 2)
#define GPT_PE_FLAG_READ_ONLY	(1ULL << 60)
#define GPT_PE_FLAG_HIDDEN	(1ULL << 62)
#define GPT_PE_FLAG_NO_AUTO	(1ULL << 63)

static unsigned long long partition_end(const struct partition *part)
{
	return part->offset + part->size;
}

static void lba_to_chs(unsigned int lba, unsigned char *chs)
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

static void hdimage_setup_chs(struct mbr_partition_entry *entry)
{
	lba_to_chs(entry->relative_sectors, entry->first_chs);
	lba_to_chs(entry->relative_sectors + entry->total_sectors - 1,
		   entry->last_chs);
}

static int hdimage_insert_mbr(struct image *image, struct list_head *partitions, int hybrid)
{
	struct hdimage *hd = image->handler_priv;
	struct mbr_tail mbr;
	struct partition *part;
	int ret, i = 0;

	if (hybrid) {
		image_info(image, "writing hybrid MBR\n");
	} else {
		image_info(image, "writing MBR\n");
	}

	memset(&mbr, 0, sizeof(mbr));
	memcpy(&mbr.disk_signature, &hd->disksig, sizeof(hd->disksig));

	list_for_each_entry(part, partitions, list) {
		struct mbr_partition_entry *entry;

		if (!part->in_partition_table)
			continue;

		if (hybrid && !part->partition_type)
			continue;

		if (hybrid && part->extended)
			continue;

		entry = &mbr.part_entry[i];

		entry->boot = part->bootable ? 0x80 : 0x00;
		if (!part->extended) {
			entry->partition_type = part->partition_type;
			entry->relative_sectors = part->offset/512;
			entry->total_sectors = part->size/512;
		}
		else {
			entry->partition_type = 0x0F;
			entry->relative_sectors = (hd->extended_lba)/512;
			entry->total_sectors = (image->size - hd->extended_lba)/512;
		}
		hdimage_setup_chs(entry);

		if (part->extended)
			break;
		i++;
	}

	if (hybrid) {
		struct mbr_partition_entry *entry;

		entry = &mbr.part_entry[i];

		entry->boot = 0x00;

		entry->partition_type = 0xee;
		entry->relative_sectors = 1;
		entry->total_sectors = hd->gpt_location / 512 + GPT_SECTORS - 2;

		hdimage_setup_chs(entry);
	}

	mbr.boot_signature = htole16(0xaa55);

	ret = insert_data(image, &mbr, imageoutfile(image), sizeof(mbr), 440);
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
	hdimage_setup_chs(entry);
	struct partition *p = part;
	list_for_each_entry_continue(p, &image->partitions, list) {
		if (!p->extended)
			continue;
		++entry;
		entry->boot = 0x00;
		entry->partition_type = 0x0F;
		entry->relative_sectors = (p->offset - hd->align - hd->extended_lba)/512;
		entry->total_sectors = (p->size + hd->align)/512;
		hdimage_setup_chs(entry);
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
	header.backup_lba = htole64(hd->gpt_no_backup ? 1 :image->size/512 - 1);
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
	ret = insert_data(image, &header, outfile, sizeof(header), 512);
	if (ret) {
		image_error(image, "failed to write GPT\n");
		return ret;
	}
	ret = insert_data(image, &table, outfile, sizeof(table), hd->gpt_location);
	if (ret) {
		image_error(image, "failed to write GPT table\n");
		return ret;
	}

	if (!hd->gpt_no_backup) {
		ret = pad_file(image, NULL, image->size, 0x0, MODE_APPEND);
		if (ret) {
			image_error(image, "failed to pad image to size %lld\n",
				    image->size);
			return ret;
		}

		header.header_crc = 0;
		header.current_lba = htole64(image->size/512 - 1);
		header.backup_lba = htole64(1);
		header.starting_lba = htole64(image->size/512 - GPT_SECTORS);
		header.header_crc = htole32(crc32(&header, sizeof(header)));
		ret = insert_data(image, &table, outfile, sizeof(table),
				  image->size - GPT_SECTORS*512);
		if (ret) {
			image_error(image, "failed to write backup GPT table\n");
			return ret;
		}
		ret = insert_data(image, &header, outfile, sizeof(header),
				  image->size - 512);
		if (ret) {
			image_error(image, "failed to write backup GPT\n");
			return ret;
		}
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

		if (child->size == 0)
			continue;

		infile = imageoutfile(child);

		ret = pad_file(image, infile, part->offset + child->size, 0x0, MODE_APPEND);

		if (ret) {
			image_error(image, "failed to write image partition '%s'\n",
					part->name);
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
	}

	if (hd->fill) {
		ret = extend_file(image, image->size);
		if (ret) {
			image_error(image, "failed to fill the image.\n");
			return ret;
		}
	}

	if (hd->partition_table)
		return reload_partitions(image);

	return 0;
}

static unsigned long long roundup(unsigned long long value, unsigned long long align)
{
	return ((value - 1)/align + 1) * align;
}

static unsigned long long rounddown(unsigned long long value, unsigned long long align)
{
	return value - (value % align);
}

static unsigned long long min_ull(unsigned long long x, unsigned long long y)
{
	return x < y ? x : y;
}

static unsigned long long max_ull(unsigned long long x, unsigned long long y)
{
	return x > y ? x : y;
}

static bool image_has_hole_covering(const char *image,
				    unsigned long long start, unsigned long long end)
{
	struct image *child;
	int i;

	if (!image)
		return false;
	child = image_get(image);
	for (i = 0; i < child->n_holes; ++i) {
		const struct extent *e = &child->holes[i];
		if (e->start <= start && end <= e->end)
			return true;
	}
	return false;
}

static int check_overlap(struct image *image, struct partition *p)
{
	unsigned long long start, end;
	struct partition *q;

	list_for_each_entry(q, &image->partitions, list) {
		/* Stop iterating when we reach p. */
		if (p == q)
			return 0;
		/* We must have that p starts beyond where q ends... */
		if (p->offset >= q->offset + q->size)
			continue;
		/* ...or vice versa. */
		if (q->offset >= p->offset + p->size)
			continue;

		/*
		 * Or maybe the image occupying the q partition has an
		 * area which it is ok to overwrite. We do not do the
		 * "vice versa" check, since images are written to the
		 * output file in the order the partitions are
		 * specified.
		 */
		start = max_ull(p->offset, q->offset);
		end = min_ull(p->offset + p->size, q->offset + q->size);

		if (image_has_hole_covering(q->image, start - q->offset, end - q->offset))
			continue;

		image_error(image,
			    "partition %s (offset 0x%llx, size 0x%llx) overlaps previous "
			    "partition %s (offset 0x%llx, size 0x%llx)\n",
			    p->name, p->offset, p->size,
			    q->name, q->offset, q->size);
		return -EINVAL;
	}
	/* This should not be reached. */
	image_error(image, "linked list corruption???");
	return -EIO;
}

static struct partition *
fake_partition(const char *name, unsigned long long offset, unsigned long long size)
{
	struct partition *p = xzalloc(sizeof(*p));

	p->name = name;
	p->offset = offset;
	p->size = size;
	p->align = 1;
	return p;
}

static int hdimage_setup(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	struct partition *autoresize_part = NULL;
	int has_extended;
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
	hd->gpt_no_backup = cfg_getbool(cfg, "gpt-no-backup");
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
		if (!hd->partition_table)
			part->in_partition_table = false;
		if (part->in_partition_table)
			++partition_table_entries;
		if (!part->align)
			part->align = part->in_partition_table ? hd->align : 1;
		if (part->in_partition_table && part->align % hd->align) {
			image_error(image, "partition alignment (%lld) of partition %s "
				    "must be multiple of image alignment (%lld)",
				    part->align, part->name, hd->align);
		}
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

	if (hd->partition_table) {
		struct partition *mbr = fake_partition("[MBR]", 512 - sizeof(struct mbr_tail),
						       sizeof(struct mbr_tail));

		list_add_tail(&mbr->list, &image->partitions);
		now = partition_end(mbr);
		if (hd->gpt) {
			struct partition *gpt_header, *gpt_array;

			gpt_header = fake_partition("[GPT header]", 512, 512);
			gpt_array = fake_partition("[GPT array]", hd->gpt_location, (GPT_SECTORS - 1) * 512);
			list_add_tail(&gpt_header->list, &image->partitions);
			list_add_tail(&gpt_array->list, &image->partitions);
			now = partition_end(gpt_array);

			if (image->size) {
				struct partition *gpt_backup;
				unsigned long long size = GPT_SECTORS * 512;

				/* Includes both the backup header and array */
				gpt_backup = fake_partition("[GPT backup]", image->size - size, size);
				list_add_tail(&gpt_backup->list, &image->partitions);
			}
		}
	}

	partition_table_entries = 0;
	list_for_each_entry(part, &image->partitions, list) {
		if (part->autoresize) {
			if (autoresize_part) {
				image_error(image, "'autoresize' is only supported "
					    "for one partition\n");
				return -EINVAL;
			}
			autoresize_part = part;
			if (image->size == 0) {
				image_error(image, "the image size must be specified "
					    "when using an 'autoresize' partition\n");
				return -EINVAL;
			}
		}
		if (hd->gpt && part->in_partition_table) {
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
		part->extended = has_extended && part->in_partition_table &&
			(partition_table_entries >= hd->extended_partition);
		if (part->extended) {
			now += hd->align;
			now = roundup(now, part->align);
		}
		if (!part->offset && part->in_partition_table) {
			part->offset = roundup(now, part->align);
		}
		if (part->extended && !hd->extended_lba)
			hd->extended_lba = part->offset - hd->align;

		if (part->offset % part->align) {
			image_error(image, "part %s offset (%lld) must be a"
					"multiple of %lld bytes\n",
					part->name, part->offset, part->align);
			return -EINVAL;
		}
		if (part->autoresize) {
			long long partsize = image->size - part->offset;
			if (hd->gpt)
				partsize -= GPT_SECTORS * 512;
			partsize = rounddown(partsize, part->align);
			if (partsize <= 0) {
				image_error(image, "partitions exceed device size\n");
				return -EINVAL;
			}
			if (partsize < (long long)part->size) {
				image_error(image, "auto-resize partition %s ends up with a size %lld"
					    " smaller than minimum %lld\n", part->name, partsize, part->size);
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
					part->size = roundup(child->size, part->align);
				else
					part->size = child->size;
			}
			if (child->size > part->size) {
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
		if (!part->extended) {
			int ret = check_overlap(image, part);
			if (ret)
				return ret;
		} else if (now > part->offset) {
			image_error(image, "part %s overlaps with previous partition\n",
					part->name);
			return -EINVAL;
		}
		if (part->in_partition_table && (part->size % 512)) {
			image_error(image, "part %s size (%lld) must be a "
					"multiple of 1 sector (512 bytes)\n",
					part->name, part->size);
			return -EINVAL;
		}
		if (part->offset + part->size > now)
			now = part->offset + part->size;
	}

	if (image->size > 0 && now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}

	if (image->size == 0) {
		if (hd->gpt) {
			now += GPT_SECTORS * 512;
			image->size = (now + 4095)/4096 * 4096;
		} else {
			image->size = now;
		}
	}

	image->handler_priv = hd;

	return 0;
}

static cfg_opt_t hdimage_opts[] = {
	CFG_STR("align", "512", CFGF_NONE),
	CFG_STR("disk-signature", "", CFGF_NONE),
	CFG_STR("disk-uuid", NULL, CFGF_NONE),
	CFG_BOOL("partition-table", cfg_true, CFGF_NONE),
	CFG_INT("extended-partition", 0, CFGF_NONE),
	CFG_BOOL("gpt", cfg_false, CFGF_NONE),
	CFG_STR("gpt-location", NULL, CFGF_NONE),
	CFG_BOOL("gpt-no-backup", cfg_false, CFGF_NONE),
	CFG_BOOL("fill", cfg_false, CFGF_NONE),
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

