/*
 * Copyright (c) 2018 Alexandre Fournier <alexandre.fournier@kiplink.fr>, Kiplink
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

#include "genimage.h"

struct qemu {
	const char *format;
	const char *extraargs;
};

static int qemu_generate(struct image *image)
{
	struct partition *part;
	struct qemu *qemu = image->handler_priv;
	char *partitions = NULL;
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		const char *infile;

		if (!part->image) {
			image_debug(image, "skipping partition %s\n",
				part->name);
			continue;
		}

		image_info(image, "adding partition %s from %s ...\n",
			part->name, part->image);

		child = image_get(part->image);
		infile = imageoutfile(child);

		if (!partitions)
			xasprintf(&partitions, "'%s'", infile);
		else
			xasprintf(&partitions, "%s '%s'", partitions, infile);
	}

	ret = systemp(image, "qemu-img convert %s -O %s %s '%s'",
			qemu->extraargs,
			qemu->format,
			partitions,
			imageoutfile(image));

	return ret;
}

static int qemu_setup(struct image *image, cfg_t *cfg)
{
	struct qemu *qemu = xzalloc(sizeof(*qemu));
	struct partition *part;
	int partitions_count = 0;

	list_for_each_entry(part, &image->partitions, list) {
		if (part->image)
			partitions_count++;
	}

	if (partitions_count == 0) {
		image_error(image, "no partition given\n");
		return -EINVAL;
	}

	qemu->format = cfg_getstr(cfg, "format");
	qemu->extraargs = cfg_getstr(cfg, "extraargs");

	image->handler_priv = qemu;

	return 0;
}

static cfg_opt_t qemu_opts[] = {
	CFG_STR("format", "qcow2", CFGF_NONE),
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler qemu_handler = {
	.type = "qemu",
	.no_rootpath = cfg_true,
	.generate = qemu_generate,
	.setup = qemu_setup,
	.opts = qemu_opts,
};
