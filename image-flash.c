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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "list.h"
#include "genimage.h"

struct flash_image {
};

static int flash_generate(struct image *image)
{
	struct partition *part;
	enum pad_mode mode = MODE_OVERWRITE;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		const char *infile;
		int ret;

		image_info(image, "writing image partition '%s' (0x%llx@0x%llx)\n",
			part->name, part->size, part->offset);

		ret = pad_file(image, NULL, part->offset, 0xFF, mode);
		if (ret) {
			image_error(image, "failed to pad image to size %lld\n",
					part->offset);
			return ret;
		}
		mode = MODE_APPEND;

		if (part->image) {
			child = image_get(part->image);
			infile = imageoutfile(child);
		} else {
			infile = NULL;
		}

		ret = pad_file(image, infile, part->offset + part->size, 0xFF, mode);
		if (ret) {
			image_error(image, "failed to write image partition '%s'\n",
					part->name);
			return ret;
		}
	}

	return 0;
}

static int flash_setup(struct image *image, cfg_t *cfg)
{
	struct flash_image *f = xzalloc(sizeof(*f));
	struct partition *part;
	int last = 0;
	unsigned long long partsize = 0, flashsize;

	image->handler_priv = f;

	if (!image->flash_type) {
		image_error(image, "no flash type given\n");
		return -EINVAL;
	}

	flashsize = (unsigned long long)image->flash_type->pebsize * image->flash_type->numpebs;

	list_for_each_entry(part, &image->partitions, list) {
		if (last) {
			image_error(image, "only last partition may have size 0\n");
			return -EINVAL;
		}

		if (!part->size) {
			last = 1;
			if (partsize > flashsize)
				goto err_exceed;
			part->size = flashsize - partsize;
		}
		if (part->size % image->flash_type->pebsize) {
			image_error(image, "part %s size (%lld) must be a "
					"multiple of erase block size (%i bytes)\n",
					part->name, part->size, image->flash_type->pebsize);
			return -EINVAL;
		}
		if (part->offset % image->flash_type->pebsize) {
			image_error(image, "part %s offset (%lld) must be a"
					"multiple of erase block size (%i bytes)\n",
					part->name, part->offset, image->flash_type->pebsize);
			return -EINVAL;
		}
		if (part->offset) {
			if (partsize > part->offset) {
				image_error(image, "part %s overlaps with previous partition\n",
					part->name);
				return -EINVAL;
			}
		} else {
			part->offset = partsize;
		}
		if (part->image) {
			struct image *child = image_get(part->image);
			if (!child) {
				image_error(image, "could not find %s\n",
						part->image);
				return -EINVAL;
			}
			if (child->size > part->size) {
				image_error(image, "part %s size (%lld) too small for %s (%lld)\n",
						part->name, part->size, child->file, child->size);
				return -EINVAL;
			}
		}

		partsize = part->offset + part->size;
	}

	if (partsize > flashsize) {
err_exceed:
		image_error(image, "size of partitions (%lld) exceeds flash size (%lld)\n",
				partsize, flashsize);
		return -EINVAL;
	}

	return 0;
}

static cfg_opt_t flash_opts[] = {
	CFG_END()
};

struct image_handler flash_handler = {
	.type = "flash",
	.generate = flash_generate,
	.setup = flash_setup,
	.opts = flash_opts,
};

