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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "genimage.h"

struct file {
	char *name;
	char *infile;
	cfg_bool_t copy;
};

static int file_parse_holes(struct image *image, cfg_t *cfg)
{
	int i;

	image->n_holes = cfg ? cfg_size(cfg, "holes") : 0;
	if (image->n_holes == 0)
		return 0;

	image->holes = xzalloc(image->n_holes * sizeof(*image->holes));
	for (i = 0; i < image->n_holes; i++) {
		const char *s = cfg_getnstr(cfg, "holes", i);
		char *start, *end;
		int len;

		if (sscanf(s, " ( %m[0-9skKMG] ; %m[0-9skKMG] ) %n", &start, &end, &len) != 2 ||
		    len != (int)strlen(s)) {
			image_error(image, "invalid hole specification '%s', use '(<start>;<end>)'\n",
				    s);
			return -EINVAL;
		}

		image->holes[i].start = strtoul_suffix(start, NULL, NULL);
		image->holes[i].end = strtoul_suffix(end, NULL, NULL);
		free(start);
		free(end);
		image_debug(image, "added hole (%llu, %llu)\n", image->holes[i].start, image->holes[i].end);
	}
	return 0;
}

static int file_generate(struct image *image)
{
	struct file *f = image->handler_priv;
	int ret;

	if (!f->copy)
		return 0;

	if (!strcmp(f->infile, imageoutfile(image)))
		return 0;

	ret = systemp(image, "cp '%s' '%s'",  f->infile, imageoutfile(image));

	return ret;
}

static int file_setup(struct image *image, cfg_t *cfg)
{
	struct file *f = xzalloc(sizeof(*f));
	struct stat s;
	int ret;

	if (cfg)
		f->name = cfg_getstr(cfg, "name");
	if (!f->name)
		f->name = strdup(image->file);

	if (f->name[0] == '/')
		f->infile = strdup(f->name);
	else
		xasprintf(&f->infile, "%s/%s", inputpath(), f->name);

	ret = stat(f->infile, &s);
	if (ret) {
		ret = -errno;
		image_error(image, "stat(%s) failed: %s\n", f->infile,
				strerror(errno));
		return ret;
	}
	if (!image->size)
		image->size = s.st_size;

	if (cfg)
		f->copy = cfg_getbool(cfg, "copy");
	else
		f->copy = cfg_false;

	if (!f->copy) {
		free(image->outfile);
		image->outfile = strdup(f->infile);
	}
	ret = file_parse_holes(image, cfg);
	if (ret)
		return ret;

	image->handler_priv = f;

	return 0;
}

static cfg_opt_t file_opts[] = {
	CFG_STR("name", NULL, CFGF_NONE),
	CFG_BOOL("copy", cfg_true, CFGF_NONE),
	CFG_STR_LIST("holes", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler file_handler = {
	.type = "file",
	.generate = file_generate,
	.setup = file_setup,
	.opts = file_opts,
};

