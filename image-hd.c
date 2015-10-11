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

#include "genimage.h"

struct hdimage {
	cfg_bool_t partition_table;
	unsigned long long align;
	unsigned long long extended_lba;
	uint32_t disksig;
};

struct partition_entry {
	unsigned char boot;

	unsigned char first_chs[3];

	unsigned char partition_type;

	unsigned char last_chs[3];

	uint32_t relative_sectors;
	uint32_t total_sectors;
} __attribute__((packed));

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

static int hdimage_setup_mbr(struct image *image, char *part_table)
{
	struct hdimage *hd = image->handler_priv;
	struct partition *part;
	int i = 0;

	image_log(image, 1, "writing MBR\n");

	*((int*)part_table) = hd->disksig;
	part_table += 6;

	list_for_each_entry(part, &image->partitions, list) {
		struct partition_entry *entry;

		if (!part->in_partition_table)
			continue;

		entry = (struct partition_entry *)(part_table + i *
				sizeof(struct partition_entry));

		entry->boot = part->bootable ? 0x80 : 0x00;
		if (!part->extended) {
			entry->partition_type = part->partition_type;
			entry->relative_sectors = part->offset/512;
			entry->total_sectors = part->size/512;
		}
		else {
			unsigned long long size = 0;
			struct partition *p = part;
			list_for_each_entry_from(p, &image->partitions, list) {
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
	part_table += 4 * sizeof(struct partition_entry);
	part_table[0] = 0x55;
	part_table[1] = 0xaa;
	return 0;
}

static int hdimage_setup_ebr(struct image *image, struct partition *part, char *ebr)
{
	struct hdimage *hd = image->handler_priv;
	struct partition_entry *entry;

	image_log(image, 1, "writing EBR\n");

	entry = (struct partition_entry *)ebr;

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

	ebr += 4 * sizeof(struct partition_entry);
	ebr[0] = 0x55;
	ebr[1] = 0xaa;
	return 0;
}

static int hdimage_generate(struct image *image)
{
	struct partition *part;
	struct hdimage *hd = image->handler_priv;
	enum pad_mode mode = MODE_OVERWRITE;
	const char *outfile = imageoutfile(image);
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		const char *infile;

		image_log(image, 1, "adding partition '%s'%s%s%s%s ...\n", part->name,
			part->in_partition_table ? " (in MBR)" : "",
			part->image ? " from '": "",
			part->image ? part->image : "",
			part->image ? "'" : "");

		if (part->image || part->extended) {
			ret = pad_file(image, NULL, outfile, part->offset, 0x0, mode);
			if (ret) {
				image_error(image, "failed to pad image to size %lld\n",
						part->offset);
				return ret;
			}
			mode = MODE_APPEND;
		}

		if (part->extended) {
			char ebr[4*sizeof(struct partition_entry)+2];
			memset(ebr, 0, sizeof(ebr));
			ret = hdimage_setup_ebr(image, part, ebr);
			ret = insert_data(image, ebr, outfile, sizeof(ebr),
					part->offset - hd->align + 446);
			if (ret) {
				image_error(image, "failed to write EBR\n");
				return ret;
			}
		}

		if (!part->image)
			continue;

		child = image_get(part->image);
		infile = imageoutfile(child);

		ret = pad_file(image, infile, outfile, child->size, 0x0, MODE_APPEND);

		if (ret) {
			image_error(image, "failed to write image partition '%s'\n",
					part->name);
			return ret;
		}
	}

	if (hd->partition_table) {
		char part_table[6+4*sizeof(struct partition_entry)+2];

		memset(part_table, 0, sizeof(part_table));
		ret = hdimage_setup_mbr(image, part_table);
		if (ret)
			return ret;

		ret = insert_data(image, part_table, outfile, sizeof(part_table), 440);
		if (ret) {
			image_error(image, "failed to write MBR\n");
			return ret;
		}
		mode = MODE_APPEND;
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
	int has_extended;
	int partition_table_entries = 0;
	unsigned long long now = 0;
	struct hdimage *hd = xzalloc(sizeof(*hd));

	hd->align = cfg_getint_suffix(cfg, "align");
	hd->partition_table = cfg_getbool(cfg, "partition-table");
	hd->disksig = strtoul(cfg_getstr(cfg, "disk-signature"), NULL, 0);

	if ((hd->align % 512) || (hd->align == 0)) {
		image_error(image, "partition alignment (%lld) must be a "
				"multiple of 1 sector (512 bytes)\n", hd->align);
		return -EINVAL;
	}
	list_for_each_entry(part, &image->partitions, list) {
		if (part->in_partition_table)
			++partition_table_entries;
	}
	has_extended = partition_table_entries > 4;
	partition_table_entries = 0;
	list_for_each_entry(part, &image->partitions, list) {
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
		/* reserve space for extended boot record if necessary */
		if (part->in_partition_table)
			++partition_table_entries;
		if (has_extended && (partition_table_entries > 3))
			part->extended = cfg_true;
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
		if (part->offset || !part->in_partition_table) {
			if (now > part->offset) {
				image_error(image, "part %s overlaps with previous partition\n",
						part->name);
				return -EINVAL;
			}
		} else {
			if (!now && hd->partition_table)
				now = 512;
			part->offset = roundup(now, hd->align);
		}
		now = part->offset + part->size;
	}

	if (image->size > 0 && now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}

	image->handler_priv = hd;

	return 0;
}

cfg_opt_t hdimage_opts[] = {
	CFG_STR("align", "512", CFGF_NONE),
	CFG_STR("disk-signature", "", CFGF_NONE),
	CFG_BOOL("partition-table", cfg_true, CFGF_NONE),
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

