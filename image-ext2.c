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

static int ext2_generate(struct image *image)
{
	int ret;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *features = cfg_getstr(image->imagesec, "features");
	char *label = cfg_getstr(image->imagesec, "label");

	ret = systemp(image, "%s -d %s --size-in-blocks=%lld -i 16384 %s %s",
			get_opt("genext2fs"),
			mountpath(image), image->size / 1024, imageoutfile(image),
			extraargs);

	if (ret)
		return ret;

	if (features && features[0] != '\0') {
		ret = systemp(image, "%s -O \"%s\" %s", get_opt("tune2fs"),
				features, imageoutfile(image));
		if (ret)
			return ret;
	}
	if (label && label[0] != '\0') {
		ret = systemp(image, "%s -L \"%s\" %s", get_opt("tune2fs"),
				label, imageoutfile(image));
		if (ret)
			return ret;
	}

	ret = systemp(image, "%s -pvfD %s", get_opt("e2fsck"),
			imageoutfile(image));

	/* e2fsck return 1 when the filesystem was successfully modified */
	return ret > 2;
}

static cfg_opt_t ext2_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", 0, CFGF_NONE),
	CFG_STR("label", 0, CFGF_NONE),
	CFG_END()
};

struct image_handler ext2_handler = {
	.type = "ext2",
	.generate = ext2_generate,
	.opts = ext2_opts,
};

static cfg_opt_t ext3_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", "has_journal", CFGF_NONE),
	CFG_STR("label", 0, CFGF_NONE),
	CFG_END()
};

struct image_handler ext3_handler = {
	.type = "ext3",
	.generate = ext2_generate,
	.opts = ext3_opts,
};

static cfg_opt_t ext4_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", "extents,uninit_bg,dir_index,has_journal", CFGF_NONE),
	CFG_STR("label", 0, CFGF_NONE),
	CFG_END()
};

struct image_handler ext4_handler = {
	.type = "ext4",
	.generate = ext2_generate,
	.opts = ext4_opts,
};

