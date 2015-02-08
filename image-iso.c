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

static int iso_generate(struct image *image)
{
	int ret;
	char *boot_image = cfg_getstr(image->imagesec, "boot-image");
	char *bootargs = cfg_getstr(image->imagesec, "bootargs");
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *input_charset = cfg_getstr(image->imagesec, "input-charset");
	char *volume_id = cfg_getstr(image->imagesec, "volume-id");

	ret = systemp(image, "%s -input-charset %s -R -hide-rr-moved %s %s %s -V '%s' %s -o %s %s",
			get_opt("genisoimage"),
			input_charset,
			boot_image ? "-b" : "",
			boot_image ? boot_image : "",
			boot_image ? bootargs : "",
			volume_id,
			extraargs,
			imageoutfile(image),
			mountpath(image));
	return ret;
}

static cfg_opt_t iso_opts[] = {
	CFG_STR("boot-image", 0, CFGF_NONE),
	CFG_STR("bootargs", "-no-emul-boot -boot-load-size 4 -boot-info-table -c boot.cat -hide boot.cat", CFGF_NONE),
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("input-charset", "default", CFGF_NONE),
	CFG_STR("volume-id", "", CFGF_NONE),
	CFG_END()
};

struct image_handler iso_handler = {
	.type = "iso",
	.generate = iso_generate,
	.opts = iso_opts,
};
