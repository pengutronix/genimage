/*
 * Copyright (c) 2012 Michael Olbrich <m.olbrich@pengutronix.de>
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

static int vfat_generate(struct image *image)
{
	int ret;
	struct partition *part;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");

	ret = systemp(image, "%s if=/dev/zero of=\"%s\" seek=%lld count=0 bs=1 2>/dev/null",
			get_opt("dd"), imageoutfile(image), image->size);
	if (ret)
		return ret;

	ret = systemp(image, "%s %s %s >/dev/null", get_opt("mkdosfs"),
			extraargs, imageoutfile(image));
	if (ret)
		return ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child = image_get(part->image);
		const char *file = imageoutfile(child);
		const char *target = part->name;
		char *path = strdupa(target);
		char *next = path;

		while ((next = strchr(next, '/')) != NULL) {
			*next = '\0';
			/* ignore the error: mdd fails if the target exists. */
			systemp(image, "%s -DsS -i %s ::%s",
				get_opt("mmd"), imageoutfile(image), path);
			*next = '/';
			++next;
		}

		image_log(image, 1, "adding file '%s' as '%s' ...\n",
				child->file, *target ? target : child->file);
		ret = systemp(image, "%s -bsp -i %s %s ::%s",
				get_opt("mcopy"), imageoutfile(image),
				file, target);
		if (ret)
			return ret;
	}
	if (!list_empty(&image->partitions))
		return 0;

	ret = systemp(image, "%s -bsp -i %s %s/* ::", get_opt("mcopy"),
			imageoutfile(image), mountpath(image));
	return ret;
}

static int vfat_parse(struct image *image, cfg_t *cfg)
{
	unsigned int i;
	unsigned int num_files;
	struct partition *part;

	num_files = cfg_size(cfg, "file");
	for (i = 0; i < num_files; i++) {
		cfg_t *filesec = cfg_getnsec(cfg, "file", i);
		part = xzalloc(sizeof *part);
		part->name = cfg_title(filesec);
		part->image = cfg_getstr(filesec, "image");
		list_add_tail(&part->list, &image->partitions);
	}

	for(i = 0; i < cfg_size(cfg, "files"); i++) {
		part = xzalloc(sizeof *part);
		part->image = cfg_getnstr(cfg, "files", i);
		part->name = "";
		list_add_tail(&part->list, &image->partitions);
	}

	return 0;
}

static cfg_opt_t file_opts[] = {
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t vfat_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR_LIST("files", 0, CFGF_NONE),
	CFG_SEC("file", file_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_END()
};

struct image_handler vfat_handler = {
	.type = "vfat",
	.generate = vfat_generate,
	.parse = vfat_parse,
	.opts = vfat_opts,
};
