/*
 * Copyright (c) 2016 Julien Viard de Galbert <julien.viarddegalbert@openwide.fr>
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

#include "genimage.h"

static int cram_generate(struct image *image)
{
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");

	return systemp(image, "%s%s%s %s '%s' '%s'",
			get_opt("mkcramfs"),
			image->name ? " -n " : "",
			image->name ? image->name : "", /* name */
			extraargs,
			mountpath(image), /* source dir */
			imageoutfile(image)); /* destination file */
}

static cfg_opt_t cram_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_END()
};

struct image_handler cramfs_handler = {
	.type = "cramfs",
	.generate = cram_generate,
	.opts = cram_opts,
};
