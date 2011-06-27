#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "genimage.h"

static int jffs2_generate(struct image *image)
{
	int ret;
	char *extraargs;

	if (!image->flash_type) {
		printf("no flash type given for %s\n", image->file);
		return -EINVAL;
	}

	extraargs = cfg_getstr(image->imagesec, "extraargs");

	ret = systemp(image, "%s --eraseblock=%d -d %s -o %s/%s %s",
			get_opt("mkfsjffs2"),
			image->flash_type->pebsize, mountpath(image), imagepath(), image->file,
			extraargs);

	return ret;
}

static int jffs2_setup(struct image *image, cfg_t *cfg)
{
	return 0;
}

static cfg_opt_t jffs2_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler jffs2_handler = {
	.type = "jffs2",
	.generate = jffs2_generate,
	.setup = jffs2_setup,
	.opts = jffs2_opts,
};

