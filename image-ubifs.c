#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "genimage.h"

static int ubifs_generate(struct image *image)
{
	int lebcount;
	int ret;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");

	lebcount = image->size / image->flash_type->lebsize;

	ret = systemp(image, "%s -d  %s -e %d -m %d -c %d -o %s/%s %s",
			get_opt("mkfsubifs"),
			mountpath(image),
			image->flash_type->lebsize,
			image->flash_type->minimum_io_unit_size,
			lebcount,
			imagepath(),
			image->file,
			extraargs);

	return ret;
}

static int ubifs_setup(struct image *image, cfg_t *cfg)
{
	return 0;
}

static cfg_opt_t ubifs_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("autoresize", "", CFGF_NONE),
	CFG_END()
};

struct image_handler ubifs_handler = {
	.type = "ubifs",
	.generate = ubifs_generate,
	.setup = ubifs_setup,
	.opts = ubifs_opts,
};

