/*
 * Copyright (c) 2025 Tobias Waldekranz <tobias@waldekranz.com>, Wires
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
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "genimage.h"

static char *verity_tmp_path(const char *verity, const char *suffix)
{
	char *path, *slug;

	slug = sanitize_path(verity);
	xasprintf(&path, "%s/%s.%s", tmppath(), slug, suffix);
	free(slug);

	return path;
}

static int verity_generate(struct image *image)
{
	const char *data, *extraargs;
	struct partition *part;
	struct stat sb;
	int ret;

	ret = prepare_image(image, image->size);
	if (ret < 0)
		return ret;

	part = list_first_entry(&image->partitions, struct partition, list);
	data = imageoutfile(image_get(part->image));

	extraargs = cfg_getstr(image->imagesec, "extraargs");

	/* As a side-effect of creating the hash tree, request that
	 * veritysetup emits the resulting root-hash into a file in
	 * tmppath(), where 'verity-sig' images that reference this
	 * 'verity' can find it.
	 */
	ret = systemp(image, "%s format --root-hash-file '%s' %s '%s' '%s'",
		      get_opt("veritysetup"),
		      verity_tmp_path(image->file, "root-hash"),
		      extraargs ? extraargs : "", data, imageoutfile(image));
	if (ret)
		return ret;

	if (stat(imageoutfile(image), &sb))
		return -errno;

	if (image->size && image->size < (unsigned long)sb.st_size) {
		image_error(image,
			    "Specified image size (%llu) is too small, generated %ld bytes\n",
			    image->size, sb.st_size);
		return -E2BIG;
	}

	image_debug(image, "generated %ld bytes\n", sb.st_size);
	image->size = sb.st_size;
	return 0;
}

static int verity_parse(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	const char *data;

	data = cfg_getstr(image->imagesec, "image");
	if (!data) {
		image_error(image, "Mandatory 'image' option is missing!\n");
		return -EINVAL;
	}

	part = xzalloc(sizeof(*part));
	part->image = data;
	list_add_tail(&part->list, &image->partitions);

	return 0;
}

static cfg_opt_t verity_opts[] = {
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_STR("extraargs", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler verity_handler = {
	.type = "verity",
	.no_rootpath = cfg_true,
	.generate = verity_generate,
	.parse = verity_parse,
	.opts = verity_opts,
};
