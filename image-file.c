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

static int file_generate(struct image *image)
{
	struct file *f = image->handler_priv;
	int ret;

	if (!f->copy)
		return 0;

	if (!strcmp(f->infile, imageoutfile(image)))
		return 0;

	ret = systemp(image, "cp %s %s",  f->infile, imageoutfile(image));

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
		asprintf(&f->infile, "%s/%s", inputpath(), f->name);

	ret = stat(f->infile, &s);
	if (ret) {
		image_error(image, "stat(%s) failed: %s\n", f->infile,
				strerror(errno));
		return -errno;
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

	image->handler_priv = f;

	return 0;
}

static cfg_opt_t file_opts[] = {
	CFG_STR("name", NULL, CFGF_NONE),
	CFG_BOOL("copy", cfg_true, CFGF_NONE),
	CFG_END()
};

struct image_handler file_handler = {
	.type = "file",
	.generate = file_generate,
	.setup = file_setup,
	.opts = file_opts,
};

