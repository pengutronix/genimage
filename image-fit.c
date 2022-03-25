/*
 * Copyright (c) 2018 Sascha Hauer <s.hauer@pengutronix.de>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "genimage.h"

static struct partition *partition_by_name(struct image *image, const char *name)
{
	struct partition *part;

	list_for_each_entry(part, &image->partitions, list)
		if (!strcmp(part->name, name))
			return part;
	return NULL;
}

static int fit_generate(struct image *image)
{
	int ret;
	struct partition *part, *its;
	char *itspath;
	int itsfd;
	char *keydir = cfg_getstr(image->imagesec, "keydir");
	char *keyopt = NULL;

	its = partition_by_name(image, "its");
	if (!its)
		return -EINVAL;

	struct image *itsimg = image_get(its->image);

	xasprintf(&itspath, "%s/fit.its", tmppath());

	/* Copy input its file to temporary path. Use 'cat' to ignore permissions */
	ret = systemp(image, "cat '%s' > '%s'", imageoutfile(itsimg), itspath);
	if (ret)
		return ret;

	itsfd = open(itspath, O_WRONLY | O_APPEND);
	if (itsfd < 0) {
		printf("Cannot open %s: %s\n", itspath, strerror(errno));
		return -errno;
	}

	dprintf(itsfd, "\n");

	/* Add /incbin/ to each /images/<partname>/ node */
	list_for_each_entry(part, &image->partitions, list) {
		struct image *child = image_get(part->image);
		const char *file = imageoutfile(child);
		const char *target = part->name;

		if (part == its)
			continue;

		dprintf(itsfd, "/ { images { %s { data = /incbin/(\"%s\"); };};};\n", target, file);
	}

	close(itsfd);

	if (keydir && *keydir) {
		if (*keydir != '/') {
			image_error(image, "'keydir' must be an absolute path\n");
			return -EINVAL;
		}
		xasprintf(&keyopt, "-k '%s'", keydir);
	}

	ret = systemp(image, "%s -r %s -f '%s' '%s'",
		get_opt("mkimage"), keyopt ? keyopt : "", itspath, imageoutfile(image));

	if (ret)
		image_error(image, "Failed to create FIT image\n");

	return ret;
}

static int fit_parse(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	char *its = cfg_getstr(image->imagesec, "its");

	part = xzalloc(sizeof *part);
	part->name = "its";
	part->image = its;
	list_add_tail(&part->list, &image->partitions);

	return 0;
}

static cfg_opt_t fit_opts[] = {
	CFG_STR("keydir", "", CFGF_NONE),
	CFG_STR("its", "", CFGF_NONE),
	CFG_END()
};

struct image_handler fit_handler = {
	.type = "fit",
	.no_rootpath = cfg_true,
	.generate = fit_generate,
	.parse = fit_parse,
	.opts = fit_opts,
};
