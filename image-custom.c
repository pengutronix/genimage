/*
 * Copyright (c) 2025 Michael Olbrich <m.olbrich@pengutronix.de>
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
#include <unistd.h>
#include <sys/stat.h>

#include "genimage.h"

struct custom {
	char *exec;
};

static int custom_generate(struct image *image)
{
	struct custom *f = image->handler_priv;
	const char *file = imageoutfile(image);
	struct stat s;
	int ret;

	ret = prepare_image(image, image->size);
	if (ret < 0)
		return ret;

	ret = systemp(image, "%s", f->exec);
	if (ret)
		return ret;

	ret = stat(file, &s);
	if (ret) {
		ret = -errno;
		if (ret == -ENOENT)
			image_error(image, "command '%s' failed to create '%s'", f->exec, file);
		else
			image_error(image, "stat(%s) failed: %s\n", file, strerror(errno));
		return ret;
	}
	if (!image->size)
		image->size = s.st_size;

	return ret;
}

static int custom_setup(struct image *image, cfg_t *cfg)
{
	struct custom *c = xzalloc(sizeof(*c));

	c->exec = cfg_getstr(cfg, "exec");

	if (!c->exec) {
		image_error(image, "mandatory option 'exec' is missing\n");
		return -EINVAL;
	}

	image->handler_priv = c;

	return 0;
}

static int custom_parse(struct image *image, cfg_t *cfg)
{
	if (!image->mountpoint || image->mountpoint[0] == '\0')
		image->empty = cfg_true;

	return 0;
}

static cfg_opt_t custom_opts[] = {
	CFG_STR("exec", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler custom_handler = {
	.type = "custom",
	.generate = custom_generate,
	.setup = custom_setup,
	.parse = custom_parse,
	.opts = custom_opts,
};
