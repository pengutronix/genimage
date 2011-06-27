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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "genimage.h"

struct file {
	char *name;
};

static int file_generate(struct image *image)
{
	struct file *f = image->handler_priv;
	int ret;

	ret = systemp(image, "cp %s/%s %s",  inputpath(), f->name, imageoutfile(image));

	return ret;
}

static int file_setup(struct image *image, cfg_t *cfg)
{
	struct file *f = xzalloc(sizeof(*f));

	f->name = cfg_getstr(cfg, "name");
	if (!f->name)
		f->name = strdup(image->file);

	image->handler_priv = f;

	return 0;
}

static cfg_opt_t file_opts[] = {
	CFG_STR("name", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler file_handler = {
	.type = "file",
	.generate = file_generate,
	.setup = file_setup,
	.opts = file_opts,
};

