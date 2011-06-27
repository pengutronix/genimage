#define _GNU_SOURCE
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

	ret = systemp(image, "%s c%s -f %s/%s -C %s/%s .",
			get_opt("tar"),
			comp,
			imagepath(), image->file, rootpath(), image->mountpoint);

	return ret;
}

static int tar_setup(struct image *image, cfg_t *cfg)
{
	return 0;
}

static cfg_opt_t tar_opts[] = {
	CFG_END()
};

struct image_handler tar_handler = {
	.type = "tar",
	.generate = tar_generate,
	.setup = tar_setup,
	.opts = tar_opts,
};

