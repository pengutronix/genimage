/*
 * Copyright (c) 2022 Tomas Mudrunka <harviecz@gmail.com>
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

static int f2fs_generate(struct image *image)
{
	int ret;

	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *label = cfg_getstr(image->imagesec, "label");

	extraargs = cfg_getstr(image->imagesec, "extraargs");

	ret = prepare_image(image, image->size);
	if(ret)
		return ret;

	ret = systemp(image, "%s %s %s%s%s %s '%s'",
			get_opt("mkfsf2fs"),
			label ? "-l"  : "",
			label ? "'" : "",
			label ? label : "",
			label ? "'" : "",
			extraargs,
			imageoutfile(image));

	if(ret || image->empty)
		return ret;

	ret = systemp(image, "%s -f '%s' '%s'",
			get_opt("sloadf2fs"),
			mountpath(image),
			imageoutfile(image));

	return ret;
}

static cfg_opt_t f2fs_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("label", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler f2fs_handler = {
	.type = "f2fs",
	.generate = f2fs_generate,
	.opts = f2fs_opts,
};
