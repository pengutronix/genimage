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

	ret = systemp(image, "%s -d %s --size-in-blocks=%lld %s %s",
			get_opt("genext2fs"),
			mountpath(image), image->size / 1024, imageoutfile(image),
			extraargs);

	if (ret)
		return ret;

	if (features) {
		ret = systemp(image, "%s -O %s %s", get_opt("tune2fs"),
				features, imageoutfile(image));
		if (ret)
			return ret;
	}

	ret = systemp(image, "%s -yf %s", get_opt("e2fsck"),
			imageoutfile(image));

	/* e2fsck return 1 when the filesystem was successfully modified */
	return ret != 1 ? ret : 0;
}

static int ext2_setup(struct image *image, cfg_t *cfg)
{
	return 0;
}

static cfg_opt_t ext2_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", 0, CFGF_NONE),
	CFG_END()
};

struct image_handler ext2_handler = {
	.type = "ext2",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext2_opts,
};

static cfg_opt_t ext3_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", "has_journal", CFGF_NONE),
	CFG_END()
};

struct image_handler ext3_handler = {
	.type = "ext3",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext3_opts,
};

static cfg_opt_t ext4_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", "extents,uninit_bg,dir_index,has_journal", CFGF_NONE),
	CFG_END()
};

struct image_handler ext4_handler = {
	.type = "ext4",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext4_opts,
};

