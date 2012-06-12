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

#include "genimage.h"

struct ubi {
};

static int ubi_generate(struct image *image)
{
	int ret;
	FILE *fini;
	char *tempfile;
	int i = 0;
	struct partition *part;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");

	asprintf(&tempfile, "%s/ubifs.ini", tmppath());
	if (!tempfile)
		return -ENOMEM;

	fini = fopen(tempfile, "w");
	if (!fini) {
		image_error(image, "creating temp file failed: %s\n", strerror(errno));
		ret = -errno;
		goto err_free;
	}

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->image);
			fclose(fini);
			ret = -EINVAL;
			goto err_free;
		}

		fprintf(fini, "[%s]\n", part->name);
		fprintf(fini, "mode=ubi\n");
		fprintf(fini, "image=%s\n", imageoutfile(child));
		fprintf(fini, "vol_id=%d\n", i);
		fprintf(fini, "vol_size=%lld\n", child->size);
		fprintf(fini, "vol_type=dynamic\n");
		fprintf(fini, "vol_name=%s\n", part->name);
		fprintf(fini, "autoresize=%s\n", part->autoresize ? "true" : "false");
		fprintf(fini, "vol_alignment=1\n");
		i++;
	}

	fclose(fini);

	ret = systemp(image, "%s -s %d -O %d -p %d -m %d -o %s %s %s",
			get_opt("ubinize"),
			image->flash_type->sub_page_size,
			image->flash_type->vid_header_offset,
			image->flash_type->pebsize,
			image->flash_type->minimum_io_unit_size,
			imageoutfile(image),
			tempfile,
			extraargs);

err_free:
	free(tempfile);

	return ret;
}

static int ubi_setup(struct image *image, cfg_t *cfg)
{
	struct ubi *ubi = xzalloc(sizeof(*ubi));
	int autoresize = 0;
	struct partition *part;

	if (!image->flash_type) {
		image_error(image, "no flash type given\n");
		return -EINVAL;
	}

	image->handler_priv = ubi;

	list_for_each_entry(part, &image->partitions, list)
		autoresize += part->autoresize;

	if (autoresize > 1) {
		image_error(image, "more than one volume has the autoresize flag set\n");
		return -EINVAL;
	}

	return 0;
}

static cfg_opt_t ubi_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler ubi_handler = {
	.type = "ubi",
	.generate = ubi_generate,
	.setup = ubi_setup,
	.opts = ubi_opts,
};

