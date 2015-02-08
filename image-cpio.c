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

static int cpio_generate(struct image *image)
{
	int ret;
	char *format = cfg_getstr(image->imagesec, "format");
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *comp = cfg_getstr(image->imagesec, "compress");

	ret = systemp(image, "(cd \"%s\" && find . | %s -H \"%s\" %s -o %s %s) > %s",
			mountpath(image),
			get_opt("cpio"),
			format, extraargs, comp[0] != '\0' ? "|" : "", comp,
			imageoutfile(image));

	return ret;
}

static cfg_opt_t cpio_opts[] = {
	CFG_STR("format", "newc", CFGF_NONE),
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("compress", "", CFGF_NONE),
	CFG_END()
};

struct image_handler cpio_handler = {
	.type = "cpio",
	.generate = cpio_generate,
	.opts = cpio_opts,
};

