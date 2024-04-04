/*
 * Copyright (c) 2024 Sebastian Muxel <sebastian.muxel@entner-electronics.com>, Entner Electronics
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
#include "genimage.h"

static int erofs_generate(struct image *image)
{
	int ret;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");


	ret = systemp(image, "%s  %s '%s' '%s'",
			get_opt("mkfserofs"),
			extraargs,
			imageoutfile(image),
			mountpath(image)
	);

	return ret;
}

static cfg_opt_t erofs_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler erofs_handler = {
	.type = "erofs",
	.generate = erofs_generate,
	.opts = erofs_opts,
};
