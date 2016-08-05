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
#include <getopt.h>

#include "genimage.h"

static LIST_HEAD(optlist);

struct config {
	const char *name;
	cfg_opt_t opt;
	const char *env;
	char cmdlineopt;
	struct list_head list;
	char *value;
	char *def;
};

/*
 * get the value of an option
 */
const char *get_opt(const char *name)
{
	struct config *c;

	list_for_each_entry(c, &optlist, list) {
		if (!strcmp(c->name, name))
			return c->value;
	}

	return NULL;
}

/*
 * set option 'name' to 'value'
 */
static int set_opt(const char *name, const char *value)
{
	struct config *c;

	list_for_each_entry(c, &optlist, list) {
		if (!strcmp(c->name, name)) {
			free(c->value);
			c->value = strdup(value);
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * Add a new option
 *
 * name:	name of the option
 * env:		environment variable this option corresponds to
 * opt:		confuse option
 * def:		default value
 */
static int add_opt(const char *name, const char *env, cfg_opt_t *opt, char *def)
{
	struct config *c = xzalloc(sizeof(*c));

	c->name = name;
	c->env = env;
	c->def = def;
	memcpy(&c->opt, opt, sizeof(cfg_opt_t));

	list_add_tail(&c->list, &optlist);

	return 0;
}

/*
 * convert all options from the options list to a cfg_opt_t
 * array suitable for confuse
 */
cfg_opt_t *get_confuse_opts(void)
{
	struct config *c;
	int num_opts = 0;;
	cfg_opt_t *opts;
	int i = 0;
	cfg_opt_t cfg_end[] = {
		CFG_END()
	};

	list_for_each_entry(c, &optlist, list) {
		if (c->opt.name)
			num_opts++;
	}

	opts = xzalloc(sizeof(cfg_opt_t) * (num_opts + 1));

	list_for_each_entry(c, &optlist, list) {
		if (c->opt.name) {
			memcpy(&opts[i], &c->opt, sizeof(cfg_opt_t));
			i++;
		}
	}

	memcpy(&opts[i], cfg_end, sizeof(cfg_opt_t));

	return opts;
}

/*
 * Get an integer type option from confuse, but with an optional
 * 'k', 'M', 'G' suffix
 */
unsigned long long cfg_getint_suffix(cfg_t *sec, const char *name)
{
	const char *str = cfg_getstr(sec, name);
	unsigned long long val = 0;

	if (str)
		val = strtoul_suffix(str, NULL, 0);

	return val;
}

/*
 * Initialize all options in the following order:
 *
 * 1) default value
 * 2) from environment
 * 3) from config file
 * 4) from command line switch
 *
 * may be called multiple times with cfg == NULL when
 * config file is not yet available.
 */
int set_config_opts(int argc, char *argv[], cfg_t *cfg)
{
	struct config *c;
	cfg_t *cfgsec = NULL;
	int num_opts = 0, n, i;
	static struct option *long_options = 0;
	int ret = 0;

	if (cfg)
		cfgsec = cfg_getsec(cfg, "config");

	list_for_each_entry(c, &optlist, list) {
		char *str;

		num_opts++;

		/* set from option default value */
		if (c->def)
			set_opt(c->name, c->def);

		/* set from environment */
		str = getenv(c->env);
		if (str)
			set_opt(c->name, str);

		/* set from config file (if already available) */
		if (cfgsec && c->opt.name) {
			str = cfg_getstr(cfgsec, c->opt.name);
			if (str)
				set_opt(c->name, str);
		}
	}

	/* and last but not least from command line switches */

	long_options = xzalloc(sizeof(struct option) * (num_opts + 1));

	i = 0;

	list_for_each_entry(c, &optlist, list) {
		struct option *o = &long_options[i];
		o->name = c->name;
		o->has_arg = 1;
		i++;
	}

	optind = 1;
	while (1) {
		int option_index = 0;

		n = getopt_long(argc, argv, "",
			long_options, &option_index);
		if (n == -1)
			break;
		switch (n) {
		case 0:
			ret = set_opt(long_options[option_index].name, optarg);
			if (ret)
				goto err_out;
			break;
		default:
			ret = -EINVAL;
			goto err_out;
		}
	}
err_out:
	free(long_options);

	return ret;
}

const char *imagepath(void)
{
	return get_opt("outputpath");
}

const char *inputpath(void)
{
	return get_opt("inputpath");
}

const char *rootpath(void)
{
	return get_opt("rootpath");
}

const char *tmppath(void)
{
	return get_opt("tmppath");
}

static struct config opts[] = {
	{
		.name = "loglevel",
		.opt = CFG_STR("loglevel", "1", CFGF_NONE),
		.env = "GENIMAGE_LOGLEVEL",
	}, {
		.name = "rootpath",
		.opt = CFG_STR("rootpath", NULL, CFGF_NONE),
		.env = "GENIMAGE_ROOTPATH",
	}, {
		.name = "tmppath",
		.opt = CFG_STR("tmppath", NULL, CFGF_NONE),
		.env = "GENIMAGE_TMPPATH",
	}, {
		.name = "inputpath",
		.opt = CFG_STR("inputpath", NULL, CFGF_NONE),
		.env = "GENIMAGE_INPUTPATH",
	}, {
		.name = "outputpath",
		.opt = CFG_STR("outputpath", NULL, CFGF_NONE),
		.env = "GENIMAGE_OUTPUTPATH",
	}, {
		.name = "cpio",
		.opt = CFG_STR("cpio", NULL, CFGF_NONE),
		.env = "GENIMAGE_CPIO",
		.def = "cpio",
	}, {
		.name = "dd",
		.opt = CFG_STR("dd", NULL, CFGF_NONE),
		.env = "GENIMAGE_DD",
		.def = "dd",
	}, {
		.name = "e2fsck",
		.opt = CFG_STR("e2fsck", NULL, CFGF_NONE),
		.env = "GENIMAGE_E2FSCK",
		.def = "e2fsck",
	}, {
		.name = "genext2fs",
		.opt = CFG_STR("genext2fs", NULL, CFGF_NONE),
		.env = "GENIMAGE_GENEXT2FS",
		.def = "genext2fs",
	}, {
		.name = "genisoimage",
		.opt = CFG_STR("genisoimage", NULL, CFGF_NONE),
		.env = "GENIMAGE_GENISOIMAGE",
		.def = "genisoimage",
	}, {
		.name = "mcopy",
		.opt = CFG_STR("mcopy", NULL, CFGF_NONE),
		.env = "GENIMAGE_MCOPY",
		.def = "mcopy",
	}, {
		.name = "mmd",
		.opt = CFG_STR("mmd", NULL, CFGF_NONE),
		.env = "GENIMAGE_MMD",
		.def = "mmd",
	}, {
		.name = "mkdosfs",
		.opt = CFG_STR("mkdosfs", NULL, CFGF_NONE),
		.env = "GENIMAGE_MKDOSFS",
		.def = "mkdosfs",
	}, {
		.name = "mkfsjffs2",
		.opt = CFG_STR("mkfsjffs2", NULL, CFGF_NONE),
		.env = "GENIMAGE_MKFJFFS2",
		.def = "mkfs.jffs2",
	}, {
		.name = "mkfsubifs",
		.opt = CFG_STR("mkfsubifs", NULL, CFGF_NONE),
		.env = "GENIMAGE_MKFSUBIFS",
		.def = "mkfs.ubifs",
	}, {
		.name = "mksquashfs",
		.opt = CFG_STR("mksquashfs", NULL, CFGF_NONE),
		.env = "GENIMAGE_MKSQUASHFS",
		.def = "mksquashfs",
	}, {
		.name = "rauc",
		.opt = CFG_STR("rauc", NULL, CFGF_NONE),
		.env = "GENIMAGE_RAUC",
		.def = "rauc",
	}, {
		.name = "tar",
		.opt = CFG_STR("tar", NULL, CFGF_NONE),
		.env = "GENIMAGE_TAR",
		.def = "tar",
	}, {
		.name = "tune2fs",
		.opt = CFG_STR("tune2fs", NULL, CFGF_NONE),
		.env = "GENIMAGE_TUNE2FS",
		.def = "tune2fs",
	}, {
		.name = "ubinize",
		.opt = CFG_STR("ubinize", NULL, CFGF_NONE),
		.env = "GENIMAGE_UBINIZE",
		.def = "ubinize",
	}, {
		.name = "config",
		.env = "GENIMAGE_CONFIG",
		.def = "genimage.cfg",
	},
};

/*
 * early setup: add all options from the array above to the
 * list of options
 */
int init_config(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		ret = add_opt(opts[i].name, opts[i].env, &opts[i].opt, opts[i].def);
		if (ret)
			return ret;
	}

	return 0;
}
