/*
 * Copyright (c) 2024 Sebastian Muxel <sebastian.muxel@entner-electronics.com>, Entner Electronics
 *           (c) 2025 Michael Olbrich <m.olbrich@pengutronix.de>
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

#include <errno.h>
#include <string.h>

#include "genimage.h"

struct erofs {
	const char *label;
};

static int erofs_generate(struct image *image)
{
	struct erofs *erofs = image->handler_priv;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	const char *fs_timestamp = cfg_getstr(image->imagesec, "fs-timestamp");
	int ret;

	ret = systemp(image, "%s %s%s%s %s%s %s '%s' '%s'",
		      get_opt("mkfserofs"),
		      erofs->label ? "-L '" : "",
		      erofs->label ? erofs->label : "",
		      erofs->label ? "'" : "",
		      fs_timestamp ? "-T " : "",
		      fs_timestamp ? fs_timestamp : "",
		      extraargs,
		      imageoutfile(image),
		      mountpath(image));

	return ret;
}

static int erofs_setup(struct image *image, cfg_t *cfg)
{
	struct erofs *erofs = xzalloc(sizeof(*erofs));
	const char *label = cfg_getstr(image->imagesec, "label");

	if (label && label[0] == '\0')
		label = NULL;

	if (label && strlen(label) > 15) {
		image_error(image, "Label '%s' is longer that allowes (15 bytes)\n", label);
		return -EINVAL;
	}

	erofs->label = label;

	image->handler_priv = erofs;
	return 0;
}

static cfg_opt_t erofs_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("label", NULL, CFGF_NONE),
	CFG_STR("fs-timestamp", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler erofs_handler = {
	.type = "erofs",
	.generate = erofs_generate,
	.setup = erofs_setup,
	.opts = erofs_opts,
};
