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

static int tar_generate(struct image *image)
{
	int ret;
	char *comp = "";

	if (strstr(image->file, ".tar.gz") || strstr(image->file, "tgz"))
		comp = "z";
	if (strstr(image->file, ".tar.bz2"))
		comp = "j";

	ret = systemp(image, "%s c%s -f %s -C %s .",
			get_opt("tar"),
			comp,
			imageoutfile(image), mountpath(image));

	return ret;
}

static cfg_opt_t tar_opts[] = {
	CFG_END()
};

struct image_handler tar_handler = {
	.type = "tar",
	.generate = tar_generate,
	.opts = tar_opts,
};

