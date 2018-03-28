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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "genimage.h"

static int ext2_generate_genext2fs(struct image *image)
{
	int ret;
	const char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	const char *features = cfg_getstr(image->imagesec, "features");
	const char *label = cfg_getstr(image->imagesec, "label");

	ret = systemp(image, "%s -d '%s' --size-in-blocks=%lld -i 16384 '%s' %s",
			get_opt("genext2fs"),
			mountpath(image), image->size / 1024, imageoutfile(image),
			extraargs);

	if (ret)
		return ret;

	if (features && features[0] != '\0') {
		ret = systemp(image, "%s -O '%s' '%s'", get_opt("tune2fs"),
				features, imageoutfile(image));
		if (ret)
			return ret;
	}
	if (label && label[0] != '\0') {
		ret = systemp(image, "%s -L '%s' '%s'", get_opt("tune2fs"),
				label, imageoutfile(image));
		if (ret)
			return ret;
	}
	return 0;
}

static int ext2_generate_mke2fs(struct image *image)
{
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *label = cfg_getstr(image->imagesec, "label");
	char *conf = cfg_getstr(image->imagesec, "mke2fs_conf");
	char *conf_env = "";

	if (label && label[0] == '\0')
		label = NULL;

	if (conf) {
		int fd;
		/* mke2fs ignores a missing config file, so make sure it exists. */
		fd = open(conf, O_RDONLY);
		if (fd < 0) {
			int ret = errno;
			image_error(image, "Failed to open mke2fs_conf '%s': %s\n",
					conf, strerror(ret));
			return -ret;
		}
		close(fd);
		xasprintf(&conf_env, "MKE2FS_CONFIG='%s' ", conf);
	}

	return systemp(image, "%s%s -t %s -E root_owner=0:0 "
			"-E lazy_itable_init=0,lazy_journal_init=0 "
			"%s -d '%s' %s %s%s '%s' %lld",
			conf_env, get_opt("mke2fs"), image->handler->type,
			image->size < 0x20000000000 ? "-O ^huge_file" : "",
			mountpath(image), extraargs,
			label ? "-L " : "", label ? label : "",
			imageoutfile(image), image->size / 1024);
}

static int ext2_generate(struct image *image)
{
	const char *fs_timestamp = cfg_getstr(image->imagesec, "fs-timestamp");
	const char *tool = cfg_getstr(image->imagesec, "tool");
	int ret;

	if (strcmp(tool, "mke2fs") == 0)
		ret = ext2_generate_mke2fs(image);
	else if (strcmp(tool, "genext2fs") == 0)
		ret = ext2_generate_genext2fs(image);
	else {
		image_error(image, "unknown tool to create %s images: %s",
			image->handler->type, tool);
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	ret = systemp(image, "%s -pvfD '%s'", get_opt("e2fsck"),
			imageoutfile(image));

	/* e2fsck return 1 when the filesystem was successfully modified */
	if  (ret > 2)
		return ret;

	if (fs_timestamp) {
		ret = systemp(image, "echo '"
			"set_current_time %s\n"
			"set_super_value mkfs_time %s\n"
			"set_super_value lastcheck %s\n"
			"set_super_value mtime 00000000' | %s -w '%s'",
			fs_timestamp, fs_timestamp, fs_timestamp,
			get_opt("debugfs"), imageoutfile(image));
		if (ret)
			return ret;
	}
	return 0;
}

static int ext2_setup(struct image *image, cfg_t *cfg)
{
	if (!image->size) {
		image_error(image, "no size given or must not be zero\n");
		return -EINVAL;
	}

	return 0;
}

static cfg_opt_t ext2_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", 0, CFGF_NONE),
	CFG_STR("label", 0, CFGF_NONE),
	CFG_STR("fs-timestamp", NULL, CFGF_NONE),
	CFG_STR("tool", "genext2fs", CFGF_NONE),
	CFG_STR("mke2fs_conf", 0, CFGF_NONE),
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
	CFG_STR("label", 0, CFGF_NONE),
	CFG_STR("fs-timestamp", NULL, CFGF_NONE),
	CFG_STR("tool", "genext2fs", CFGF_NONE),
	CFG_STR("mke2fs_conf", 0, CFGF_NONE),
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
	CFG_STR("label", 0, CFGF_NONE),
	CFG_STR("fs-timestamp", NULL, CFGF_NONE),
	CFG_STR("tool", "genext2fs", CFGF_NONE),
	CFG_STR("mke2fs_conf", 0, CFGF_NONE),
	CFG_END()
};

struct image_handler ext4_handler = {
	.type = "ext4",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext4_opts,
};

