/*
 * Copyright (c) 2011 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "genimage.h"

static int hdimage_generate(struct image *image)
{
	struct partition *part;
	enum pad_mode mode = MODE_OVERWRITE;
	const char *outfile = imageoutfile(image);
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		const char *infile;

		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->image);
			return -EINVAL;
		}
		infile = imageoutfile(child);

		if (part->offset) {
			ret = pad_file(NULL, outfile, part->offset, 0x0, mode);
			if (ret) {
				image_error(image, "failed to pad image to size %lld\n",
						part->offset);
				return ret;
			}
			mode = MODE_APPEND;
		}

		ret = pad_file(infile, outfile, part->size, 0x0, mode);

		if (ret)
			return ret;
		mode = MODE_APPEND;
	}

	return 0;
}

static int hdimage_setup(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	unsigned long long now = 0;

	list_for_each_entry(part, &image->partitions, list) {
		if (part->offset) {
			if (now > part->offset) {
				image_error(image, "part %s overlaps with previous partition\n",
						part->name);
				return -EINVAL;
			}
			now = part->offset + part->size;		
		} else {
			now = now + part->size;
		}
	}

	if (now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}
	return 0;
}

cfg_opt_t hdimage_opts[] = {
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

