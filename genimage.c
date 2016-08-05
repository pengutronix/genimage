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

#include <stdio.h>
#include <confuse.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/types.h>
#include <dirent.h>

#include "genimage.h"

/*
 * TODO:
 *
 * - add documentation
 * - implement missing image types (cpio, iso)
 * - free memory after usage
 * - make more failsafe (does flashtype exist where necessary)
 * - implement command line switches (--verbose, --dry-run, --config=)
 *
 */
static struct image_handler *handlers[] = {
	&cpio_handler,
	&ext2_handler,
	&ext3_handler,
	&ext4_handler,
	&file_handler,
	&flash_handler,
	&hdimage_handler,
	&iso_handler,
	&jffs2_handler,
	&rauc_handler,
	&squashfs_handler,
	&tar_handler,
	&ubi_handler,
	&ubifs_handler,
	&vfat_handler,
};

static int image_set_handler(struct image *image, cfg_t *cfg)
{
	unsigned int i;
	int num = 0, x;

	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		struct image_handler *handler = handlers[i];

		x = cfg_size(cfg, handler->type);
		if (x)
			image->handler = handler;
		num += x;
	}

	if (num > 1) {
		image_error(image, "multiple image types given\n");
		exit (1);
	}

	if (num < 1) {
		image_error(image, "no image type given\n");
		exit (1);
	}

	image->imagesec = cfg_getsec(cfg, image->handler->type);

	return 0;
}

static cfg_opt_t partition_opts[] = {
	CFG_STR("offset", NULL, CFGF_NONE),
	CFG_STR("size", NULL, CFGF_NONE),
	CFG_INT("partition-type", 0, CFGF_NONE),
	CFG_BOOL("bootable", cfg_false, CFGF_NONE),
	CFG_BOOL("read-only", cfg_false, CFGF_NONE),
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_BOOL("autoresize", 0, CFGF_NONE),
	CFG_BOOL("in-partition-table", cfg_true, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t image_common_opts[] = {
	CFG_STR("name", NULL, CFGF_NONE),
	CFG_STR("size", NULL, CFGF_NONE),
	CFG_STR("mountpoint", NULL, CFGF_NONE),
	CFG_STR("exec-pre", NULL, CFGF_NONE),
	CFG_STR("exec-post", NULL, CFGF_NONE),
	CFG_STR("flashtype", NULL, CFGF_NONE),
	CFG_SEC("partition", partition_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_FUNC("include", &cfg_include),
};

static cfg_opt_t flashchip_opts[] = {
	CFG_STR("pebsize", "", CFGF_NONE),
	CFG_STR("lebsize", "", CFGF_NONE),
	CFG_STR("numpebs", "", CFGF_NONE),
	CFG_STR("minimum-io-unit-size", "", CFGF_NONE),
	CFG_STR("vid-header-offset", "", CFGF_NONE),
	CFG_STR("sub-page-size", "", CFGF_NONE),
	CFG_END()
};

static LIST_HEAD(images);

/*
 * find an image corresponding to a filename
 */
struct image *image_get(const char *filename)
{
	struct image *image;

	list_for_each_entry(image, &images, list) {
		if (!strcmp(image->file, filename))
			return image;
	}
	return NULL;
}

/*
 * setup the images. Calls ->setup function for each
 * image, recursively calls itself for resolving dependencies
 */
static int image_setup(struct image *image)
{
	int ret;
	struct partition *part;

	if (image->done < 0)
		return 0;

	if (image->seen < 0) {
		image_error(image, "recursive dependency detected\n");
		return -EINVAL;
	}

	image->seen = -1;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		if (!part->image)
			continue;
		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->image);
			return -EINVAL;
		}
		ret = image_setup(child);
		if (ret) {
			image_error(image, "could not setup %s\n", child->file);
			return ret;
		}
	}
	if (image->handler->setup)
		ret = image->handler->setup(image, image->imagesec);

	if (ret)
		return ret;

	image->done = -1;

	return 0;
}

/*
 * generate the images. Calls ->generate function for each
 * image, recursively calls itself for resolving dependencies
 */
static int image_generate(struct image *image)
{
	int ret;
	struct partition *part;

	if (image->done > 0)
		return 0;

	if (image->seen > 0) {
		image_error(image, "recursive dependency detected\n");
		return -EINVAL;
	}

	image->seen = 1;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		if (!part->image)
			continue;
		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->image);
			return -EINVAL;
		}
		ret = image_generate(child);
		if (ret) {
			image_error(image, "could not generate %s\n", child->file);
			return ret;
		}
	}

	if (image->exec_pre) {
		ret = systemp(image, "%s", image->exec_pre);
		if (ret)
			return ret;
	}

	if (image->handler->generate) {
		ret = image->handler->generate(image);
	} else {
		image_error(image, "no generate function for %s\n", image->file);
		return -EINVAL;
	}

	if (ret) {
		systemp(image, "rm -f %s", imageoutfile(image));
		return ret;
	}

	if (image->exec_post) {
		ret = systemp(image, "%s", image->exec_post);
		if (ret)
			return ret;
	}

	image->done = 1;

	return 0;
}

static LIST_HEAD(flashlist);

static int parse_flashes(cfg_t *cfg)
{
	int num_flashes;
	int i;

	num_flashes = cfg_size(cfg, "flash");

	for (i = 0; i < num_flashes; i++) {
		cfg_t *flashsec = cfg_getnsec(cfg, "flash", i);
		struct flash_type *flash = xzalloc(sizeof *flash);

		flash->name = cfg_title(flashsec);
		flash->pebsize = cfg_getint_suffix(flashsec, "pebsize");
		flash->lebsize = cfg_getint_suffix(flashsec, "lebsize");
		flash->numpebs = cfg_getint_suffix(flashsec, "numpebs");
		flash->minimum_io_unit_size = cfg_getint_suffix(flashsec, "minimum-io-unit-size");
		flash->vid_header_offset = cfg_getint_suffix(flashsec, "vid-header-offset");
		flash->sub_page_size = cfg_getint_suffix(flashsec, "sub-page-size");
		list_add_tail(&flash->list, &flashlist);
	}

	return 0;
}

struct flash_type *flash_type_get(const char *name)
{
	struct flash_type *flash;

	list_for_each_entry(flash, &flashlist, list) {
		if (!strcmp(flash->name, name))
			return flash;
	}
	return NULL;
}

static int parse_partitions(struct image *image, cfg_t *imagesec)
{
	struct partition *part;
	int num_partitions;
	int i;

	num_partitions = cfg_size(imagesec, "partition");

	for (i = 0; i < num_partitions; i++) {
		cfg_t *partsec = cfg_getnsec(imagesec, "partition", i);

		part = xzalloc(sizeof *part);
		part->name = cfg_title(partsec);
		list_add_tail(&part->list, &image->partitions);
		part->size = cfg_getint_suffix(partsec, "size");
		part->offset = cfg_getint_suffix(partsec, "offset");
		part->partition_type = cfg_getint(partsec, "partition-type");
		part->bootable = cfg_getbool(partsec, "bootable");
		part->read_only = cfg_getbool(partsec, "read-only");
		part->image = cfg_getstr(partsec, "image");
		part->autoresize = cfg_getbool(partsec, "autoresize");
		part->in_partition_table = cfg_getbool(partsec, "in-partition-table");
        }

	return 0;
}

static int set_flash_type(void)
{
	struct image *image;

	list_for_each_entry(image, &images, list) {
		struct partition *part;
		if (!image->flash_type)
			continue;
		list_for_each_entry(part, &image->partitions, list) {
			struct image *i;
			if (!part->image)
				continue;
			i = image_get(part->image);
			if (!i)
				return -EINVAL;
			if (i->flash_type) {
				if (i->flash_type != image->flash_type) {
					image_error(i, "conflicting flash types: %s has flashtype %s whereas %s has flashtype %s\n",
							i->file, i->flash_type->name, image->file, image->flash_type->name);
					return -EINVAL;
				}
			} else {
				i->flash_type = image->flash_type;
			}
		}
	}
	return 0;
}

static LIST_HEAD(mountpoints);

static struct mountpoint *get_mountpoint(const char *path)
{
	struct mountpoint *mp;

	list_for_each_entry(mp, &mountpoints, list) {
		if (!strcmp(mp->path, path))
			return mp;
	}
	return NULL;
}

static struct mountpoint *add_mountpoint(const char *path)
{
	struct mountpoint *mp;

	mp = get_mountpoint(path);
	if (mp)
		return mp;

	mp = xzalloc(sizeof(*mp));
	mp->path = strdup(path);
	asprintf(&mp->mountpath, "%s/%s", tmppath(), mp->path);
	list_add_tail(&mp->list, &mountpoints);

	return mp;
}

static void add_root_mountpoint(void)
{
	struct mountpoint *mp;

	mp = xzalloc(sizeof(*mp));
	mp->path = strdup("");
	asprintf(&mp->mountpath, "%s/root", tmppath());
	list_add_tail(&mp->list, &mountpoints);
}

static int collect_mountpoints(void)
{
	struct image *image;
	struct mountpoint *mp;
	int ret;

	add_root_mountpoint();

	ret = systemp(NULL, "mkdir -p %s", tmppath());
	if (ret)
		return ret;

	ret = systemp(NULL, "cp -a %s %s/root", rootpath(), tmppath());
	if (ret)
		return ret;

	list_for_each_entry(image, &images, list) {
		if (image->mountpoint)
			image->mp = add_mountpoint(image->mountpoint);
	}

	list_for_each_entry(mp, &mountpoints, list) {
		if (!strlen(mp->path))
			continue;
		ret = systemp(NULL, "mv %s/root/%s %s", tmppath(), mp->path, tmppath());
		if (ret)
			return ret;
		ret = systemp(NULL, "mkdir %s/root/%s", tmppath(), mp->path);
		if (ret)
			return ret;
		ret = systemp(NULL, "chmod --reference=%s %s/root/%s", mp->mountpath, tmppath(), mp->path);
		if (ret)
			return ret;
		ret = systemp(NULL, "chown --reference=%s %s/root/%s", mp->mountpath, tmppath(), mp->path);
		if (ret)
			return ret;
	}

	return 0;
}

const char *mountpath(struct image *image)
{
	struct mountpoint *mp;

	mp = image->mp;
	if (!mp)
		mp = get_mountpoint("");

	return mp->mountpath;
}

static int tmppath_generated;

static void check_tmp_path(void)
{
	const char *tmp = tmppath();
	int ret;
	DIR *dir;
	int i = 0;

	if (!tmp) {
		error("tmppath not set. aborting\n");
		exit(1);
	}

	dir = opendir(tmp);
	if (!dir) {
		ret = systemp(NULL, "mkdir -p %s", tmppath());
		if (ret)
			exit(1);
		closedir(dir);
		return;
	}

	while (1) {
		if (!readdir(dir))
			break;
		i++;
		if (i > 2) {
			error("tmppath '%s' exists and is not empty\n", tmp);
			exit(1);
		}
	}
	tmppath_generated = 1;
	closedir(dir);
}

static void cleanup(void)
{
	if (tmppath_generated)
		systemp(NULL, "rm -rf %s/*", tmppath());
}

static cfg_opt_t top_opts[] = {
	CFG_SEC("image", NULL, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("flash", flashchip_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("config", NULL, CFGF_MULTI),
	CFG_FUNC("include", &cfg_include),
	CFG_END()
};

static int overwriteenv(const char *name, const char *value)
{
	int ret;

	ret = setenv(name, value ? : "", 1);
	if (ret)
		return -errno;

	return 0;
}

static int setenv_paths(void)
{
	int ret;

	ret = overwriteenv("OUTPUTPATH", imagepath());
	if (ret)
		return ret;

	ret = overwriteenv("INPUTPATH", inputpath());
	if (ret)
		return ret;

	ret = overwriteenv("ROOTPATH", rootpath());
	if (ret)
		return ret;

	ret = overwriteenv("TMPPATH", tmppath());
	if (ret)
		return ret;

	return 0;
}

static int setenv_image(const struct image *image)
{
	int ret;
	char sizestr[20];

	snprintf(sizestr, sizeof(sizestr), "%llu", image->size);

	ret = overwriteenv("IMAGE", image->file);
	if (ret)
		return ret;

	ret = overwriteenv("IMAGEOUTFILE", imageoutfile(image));
	if (ret)
		return ret;

	ret = overwriteenv("IMAGENAME", image->name);
	if (ret)
		return ret;

	ret = overwriteenv("IMAGESIZE", sizestr);
	if (ret)
		return ret;

	ret = overwriteenv("IMAGEMOUNTPOINT", image->mountpoint);
	if (ret)
		return ret;

	return 0;
}

int main(int argc, char *argv[])
{
	unsigned int i;
	unsigned int num_images;
	int ret;
	cfg_opt_t *imageopts = xzalloc((ARRAY_SIZE(image_common_opts) +
				ARRAY_SIZE(handlers) + 1) * sizeof(cfg_opt_t));;
	int start;
	struct image *image;
	char *str;
	cfg_t *cfg;
	struct partition *part;

	cfg_opt_t image_end[] = {
		CFG_END()
	};

	memcpy(imageopts, image_common_opts, sizeof(image_common_opts));

	start = ARRAY_SIZE(image_common_opts);
	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		struct image_handler *handler = handlers[i];
		cfg_opt_t image_tmp[] = {
			CFG_SEC("dummy", NULL, CFGF_MULTI),
		};

		image_tmp[0].name = handler->type;
		image_tmp[0].subopts = handler->opts;
		
		memcpy(&imageopts[start + i], image_tmp, sizeof(cfg_opt_t));
	}

	memcpy(&imageopts[start + i], &image_end[0], sizeof(cfg_opt_t));

	top_opts[0].subopts = imageopts;

	init_config();

	top_opts[2].subopts = get_confuse_opts();

	/* call set_config_opts to make get_opt("config") work */
	set_config_opts(argc, argv, NULL);

	cfg = cfg_init(top_opts, CFGF_NONE);

	ret = cfg_parse(cfg, get_opt("config"));
	switch (ret) {
	case 0:
			break;
	case CFG_PARSE_ERROR:
		goto cleanup;
	case CFG_FILE_ERROR:
		error("could not open config file '%s'\n", get_opt("config"));
		goto cleanup;
	}

	/* again, with config file this time */
	set_config_opts(argc, argv, cfg);

	check_tmp_path();

	ret = systemp(NULL, "rm -rf %s/*", tmppath());
	if (ret)
		goto cleanup;

	parse_flashes(cfg);

	num_images = cfg_size(cfg, "image");

	for (i = 0; i < num_images; i++) {
		cfg_t *imagesec = cfg_getnsec(cfg, "image", i);
		image = xzalloc(sizeof *image);
		INIT_LIST_HEAD(&image->partitions);
		list_add_tail(&image->list, &images);
		image->file = cfg_title(imagesec);
		image->name = cfg_getstr(imagesec, "name");
		image->size = cfg_getint_suffix(imagesec, "size");
		image->mountpoint = cfg_getstr(imagesec, "mountpoint");
		image->exec_pre = cfg_getstr(imagesec, "exec-pre");
		image->exec_post = cfg_getstr(imagesec, "exec-post");
		asprintf(&image->outfile, "%s/%s", imagepath(), image->file);
		if (image->mountpoint && *image->mountpoint == '/')
			image->mountpoint++;
		str = cfg_getstr(imagesec, "flashtype");
		if (str)
			image->flash_type = flash_type_get(str);
		image_set_handler(image, imagesec);
		parse_partitions(image, imagesec);
		if (image->handler->parse) {
			ret = image->handler->parse(image, image->imagesec);
			if (ret)
				goto cleanup;
		}
        }

	/* check if each partition has a corresponding image */
	list_for_each_entry(image, &images, list) {
		list_for_each_entry(part, &image->partitions, list) {
			struct image *child;

			if (!part->image) {
				if (part->in_partition_table)
					continue;
				image_error(image, "no input file given\n");
				ret = -EINVAL;
				goto cleanup;
			}

			child = image_get(part->image);
			if (child)
				continue;
			image_log(image, 2, "adding implicit file rule for '%s'\n", part->image);
			child = xzalloc(sizeof *image);
			INIT_LIST_HEAD(&child->partitions);
			list_add_tail(&child->list, &images);
			child->file = part->image;
			child->handler = &file_handler;
		}
	}

	/* propagate flash types to partitions */
	ret = set_flash_type();
	if (ret)
		goto cleanup;

	list_for_each_entry(image, &images, list) {
		ret = image_setup(image);
		if (ret)
			goto cleanup;
	}

	ret = setenv_paths();
	if (ret)
		goto cleanup;

	ret = systemp(NULL, "mkdir -p %s", imagepath());
	if (ret)
		goto cleanup;

	ret = collect_mountpoints();
	if (ret)
		goto cleanup;

	list_for_each_entry(image, &images, list) {
		ret = setenv_image(image);
		if (ret)
			goto cleanup;

		ret = image_generate(image);
		if (ret) {
			image_error(image, "failed to generate %s\n", image->file);
			goto cleanup;
		}
	}

cleanup:
	cleanup();
	return ret ? 1 : 0;
}
