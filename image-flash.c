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
	struct stat s;
	int ret;
	const char *buf;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;

		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->name);
			return -EINVAL;
		}
		buf = imageoutfile(child);
		ret = stat(buf, &s);
		if (ret)
			return -errno;

		if (s.st_size > part->size) {
			image_error(image, "image file %s for partition %s is bigger than partition (%lld > %lld)\n",
					child->file, part->name, (long long)s.st_size, part->size);
			return -EINVAL;
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

	flashsize = image->flash_type->pebsize * image->flash_type->numpebs;

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

		partsize += part->size;
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

