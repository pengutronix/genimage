/*
 * Copyright (c) 2014 Juergen Beisert <jbe@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "genimage.h"

static int squash_generate(struct image *image)
{
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char compression[128];
	char *comp_setup = cfg_getstr(image->imagesec, "compression");
	unsigned block_size = cfg_getint_suffix(image->imagesec, "block-size");

	/*
	 * 'mksquashfs' currently defaults to 'gzip' compression. Provide a shortcut
	 * to be able to disable all kind of compression and force the current
	 * default behaviour for the future. Disabling compression is very useful
	 * to handle binary diffs.
	 */
	if (!strcasecmp(comp_setup, "none"))
		strncpy(compression, "-comp gzip -noInodeCompression -noDataCompression -noFragmentCompression -noXattrCompression", sizeof(compression));
	else
		snprintf(compression, sizeof(compression), "-comp %s", comp_setup);

	return systemp(image, "%s %s %s -b %u -noappend %s %s",
			get_opt("mksquashfs"),
			mountpath(image), /* source dir */
			imageoutfile(image), /* destination file */
			block_size, compression, extraargs);
}

/**
 * 'compression' can be 'gzip' (the current default), 'lzo', 'xz' or 'none'
 * @note 'none' is a special keyword to add the parameters '-noInodeCompression -noDataCompression -noFragmentCompression -noXattrCompression'
 */
static cfg_opt_t squash_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("compression", "gzip", CFGF_NONE),
	CFG_STR("block-size", "4096", CFGF_NONE),
	CFG_END()
};

struct image_handler squashfs_handler = {
	.type = "squashfs",
	.generate = squash_generate,
	.opts = squash_opts,
};
