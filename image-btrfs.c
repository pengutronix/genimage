/*
 * Copyright (c) 2022 Tomas Mudrunka <harviecz@gmail.com>
 * Copyright (c) 2024 Fiona Klute <fiona.klute@gmx.de>
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

static int btrfs_generate(struct image *image)
{
	int ret;

	const char *label = cfg_getstr(image->imagesec, "label");
	const char *extraargs = cfg_getstr(image->imagesec, "extraargs");

	ret = prepare_image(image, image->size);
	if (ret)
		return ret;

	ret = systemp(image, "%s %s %s %s %s%s%s %s '%s'",
		      get_opt("mkfsbtrfs"),
		      label ? "-L" : "",
		      label ? label : "",
		      /* initial filesystem content, if any */
		      image->empty ? "" : "-r",
		      image->empty ? "" : "'",
		      image->empty ? "" : mountpath(image),
		      image->empty ? "" : "'",
		      extraargs,
		      imageoutfile(image)); /* destination file */

	if (ret || image->empty)
		return ret;

	return ret;
}

static cfg_opt_t btrfs_opts[] = {
	CFG_STR("label", NULL, CFGF_NONE),
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler btrfs_handler = {
	.type = "btrfs",
	.generate = btrfs_generate,
	.opts = btrfs_opts,
};
