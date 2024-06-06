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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "genimage.h"

struct ext {
	int use_mke2fs;
	const char *features;
	char *usage_type_args;
	char *conf_env;
	char *size_features;
};

static int ext2_generate_genext2fs(struct image *image)
{
	int ret;
	struct ext *ext = image->handler_priv;
	const char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	const char *label = cfg_getstr(image->imagesec, "label");

	ret = systemp(image, "%s %s%s%s --size-in-blocks=%lld -i 16384 '%s' %s",
			get_opt("genext2fs"),
			image->empty ? "" : "-d '",
			image->empty ? "" : mountpath(image),
			image->empty ? "" : "'",
			image->size / 1024, imageoutfile(image), extraargs);

	if (ret)
		return ret;

	if (ext->features && ext->features[0] != '\0') {
		ret = systemp(image, "%s -O '%s' '%s'", get_opt("tune2fs"),
				ext->features, imageoutfile(image));
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
	struct ext *ext = image->handler_priv;
	const char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	const char *label = cfg_getstr(image->imagesec, "label");
	const char *root_owner = cfg_getstr(image->imagesec, "root-owner");
	const char *options = "lazy_itable_init=0,lazy_journal_init=0";
	const char *features = ext->features;
	int ret;

	if (features && features[0] == '\0')
		features = NULL;
	if (label && label[0] == '\0')
		label = NULL;

	ret = prepare_image(image, image->size);
	if (ret < 0)
		return ret;

	return systemp(image, "%s%s -t %s%s -I 256 -E 'root_owner=%s,%s'%s %s%s%s %s %s%s%s %s%s%s '%s' %lldk",
			ext->conf_env, get_opt("mke2fs"), image->handler->type,
			ext->usage_type_args, root_owner, options, ext->size_features,
			image->empty ? "" : "-d '",
			image->empty ? "" : mountpath(image),
			image->empty ? "" : "'",
			extraargs,
			label ? "-L '" : "",
			label ? label : "",
			label ? "'" : "",
			features ? "-O '" : "",
			features ? features : "",
			features ? "'" : "",
			imageoutfile(image), image->size / 1024);
}

static int ext2_generate(struct image *image)
{
	struct ext *ext = image->handler_priv;
	const char *fs_timestamp = cfg_getstr(image->imagesec, "fs-timestamp");
	int ret;

	if (ext->use_mke2fs)
		ret = ext2_generate_mke2fs(image);
	else
		ret = ext2_generate_genext2fs(image);

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
	struct ext *ext = xzalloc(sizeof(*ext));
	const char *conf = cfg_getstr(image->imagesec, "mke2fs-conf");
	const char *usage_type = cfg_getstr(image->imagesec, "usage-type");

	if (!conf) {
		conf = cfg_getstr(image->imagesec, "mke2fs_conf");
		if (conf)
			image_info(image, "option 'mke2fs_conf' is deprecated, use mke2fs-conf instead.\n");
	}

	if (!image->size) {
		image_error(image, "no size given or must not be zero\n");
		return -EINVAL;
	}

	ext->use_mke2fs = cfg_getbool(cfg, "use-mke2fs");

	ext->features = cfg_getstr(image->imagesec, "features");
	if (!ext->features) {
		if (!ext->use_mke2fs) {
			if (!strcmp(image->handler->type, "ext3"))
				ext->features = "has_journal";
			else if (!strcmp(image->handler->type, "ext4"))
				ext->features = "extents,uninit_bg,dir_index,has_journal";
		}
	}

	if (ext->use_mke2fs) {
		int is_large = image->size >= 4ll * 1024 * 1024 * 1024;
		int is_huge = image->size >= 2048ll * 1024 * 1024 * 1024;
		struct stat s;
		int ret;

		if (conf) {
			/* mke2fs ignores a missing config file, so make sure it exists. */
			ret = stat(conf, &s);
			if (ret) {
				image_error(image, "mke2fs.conf(%s) does not exist: %s\n",
						conf, strerror(errno));
				return -errno;
			}
			xasprintf(&ext->conf_env,"MKE2FS_CONFIG=\"%s\" ", conf);
		} else
			ext->conf_env = "";

		if (usage_type)
			xasprintf(&ext->usage_type_args, " -T '%s'", usage_type);
		else
			ext->usage_type_args = "";

		xasprintf(&ext->size_features, "%s%s",
			is_large ? "" : " -O '^large_file'",
			is_huge ? "" :  " -O '^huge_file'");
	}
	else {
		if (conf) {
			image_error(image, "'mke2fs.conf' is only used for 'mke2fs'\n");
			return -EINVAL;
		}
		if (usage_type) {
			image_error(image, "'usage_type' is only used for 'mke2fs'\n");
			return -EINVAL;
		}
	}

	image->handler_priv = ext;

	return 0;
}

static cfg_opt_t ext_opts[] = {
	CFG_STR("root-owner", "0:0", CFGF_NONE),
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR("features", NULL, CFGF_NONE),
	CFG_STR("label", NULL, CFGF_NONE),
	CFG_STR("fs-timestamp", NULL, CFGF_NONE),
	CFG_BOOL("use-mke2fs", cfg_true, CFGF_NONE),
	CFG_STR("usage-type", NULL, CFGF_NONE),
	CFG_STR("mke2fs-conf", NULL, CFGF_NONE),
	CFG_STR("mke2fs_conf", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler ext2_handler = {
	.type = "ext2",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext_opts,
};

struct image_handler ext3_handler = {
	.type = "ext3",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext_opts,
};

struct image_handler ext4_handler = {
	.type = "ext4",
	.generate = ext2_generate,
	.setup = ext2_setup,
	.opts = ext_opts,
};

