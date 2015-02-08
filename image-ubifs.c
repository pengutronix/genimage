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

#include "genimage.h"

static int ubifs_generate(struct image *image)
{
	int max_leb_cnt;
	int ret;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	unsigned long long max_size = cfg_getint_suffix(image->imagesec, "max-size");

	if (max_size)
		max_leb_cnt = max_size / image->flash_type->lebsize;
	else
		max_leb_cnt = image->size / image->flash_type->lebsize;

	ret = systemp(image, "%s -d  %s -e %d -m %d -c %d -o %s %s",
			get_opt("mkfsubifs"),
			mountpath(image),
			image->flash_type->lebsize,
			image->flash_type->minimum_io_unit_size,
			max_leb_cnt,
			imageoutfile(image),
			extraargs);

	return ret;
}

static int ubifs_setup(struct image *image, cfg_t *cfg)
{
	if (!image->flash_type) {
		image_error(image, "no flash type given\n");
		return -EINVAL;
	}

	return 0;
}

static cfg_opt_t ubifs_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("max-size", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler ubifs_handler = {
	.type = "ubifs",
	.generate = ubifs_generate,
	.setup = ubifs_setup,
	.opts = ubifs_opts,
};

