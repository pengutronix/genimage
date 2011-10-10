#define _GNU_SOURCE
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

	ret = systemp(image, "%s -d %s --size-in-blocks=%lld %s %s",
			get_opt("genext2fs"),
			mountpath(image), image->size / 1024, imageoutfile(image),
			extraargs);

	return ret;
}

static int ext2_setup(struct image *image, cfg_t *cfg)
{
	return 0;
}

static cfg_opt_t ext2_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler ext2_handler = {
	.type = "ext2",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext2_opts,
};

