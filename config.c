#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

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

const char *get_opt(const char *name)
{
	struct config *c;

	list_for_each_entry(c, &optlist, list) {
		if (!strcmp(c->name, name))
			return c->value;
	}

	return NULL;
}

int add_opt(const char *name, char cmdline, const char *env, cfg_opt_t *opt, char *def)
{
	struct config *c = xzalloc(sizeof(*c));

	c->name = strdup(name);
	c->cmdlineopt = cmdline;
	c->env = env;
	c->def = def;
	memcpy(&c->opt, opt, sizeof(cfg_opt_t));

	list_add_tail(&c->list, &optlist);

	return 0;
}

cfg_opt_t *get_config_opts(void)
{
	struct config *c;
	int num_opts = 0;;
	cfg_opt_t *opts;
	int i = 0;
	cfg_opt_t cfg_end[] = {
		CFG_END()
	};

	list_for_each_entry(c, &optlist, list)
		num_opts++;

	opts = xzalloc(sizeof(cfg_opt_t) * (num_opts + 1));

	list_for_each_entry(c, &optlist, list) {
		memcpy(&opts[i], &c->opt, sizeof(cfg_opt_t));
		i++;
	}

	memcpy(&opts[i], cfg_end, sizeof(cfg_opt_t));

	return opts;
}

int set_config_opts(int argc, char *argv[], cfg_t *cfg)
{
	struct config *c;
	cfg_t *cfgsec;

	list_for_each_entry(c, &optlist, list) {
		char *str = c->def;
		if (str)
			c->value = strdup(str);
	}

	list_for_each_entry(c, &optlist, list) {
		char *str = getenv(c->env);
		if (str) {
			if (c->value)
				free(c->value);
			c->value = strdup(str);
		}
	}

	cfgsec = cfg_getsec(cfg, "config");
	if (cfgsec) {
		list_for_each_entry(c, &optlist, list) {
			char *str = cfg_getstr(cfgsec, c->opt.name);
			if (str) {
				if (c->value)
					free(c->value);
				c->value = strdup(str);
			}
		}
	}

	return 0;
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
		.name = "rootpath",
		.opt = CFG_STR("rootpath", NULL, CFGF_NONE),
		.cmdlineopt = 'r',
		.env = "PTXMKIMAGE_ROOTPATH",
	}, {
		.name = "tmppath",
		.opt = CFG_STR("tmppath", NULL, CFGF_NONE),
		.cmdlineopt = 't',
		.env = "PTXMKIMAGE_TMPPATH",
	}, {
		.name = "inputpath",
		.opt = CFG_STR("inputpath", NULL, CFGF_NONE),
		.cmdlineopt = 'i',
		.env = "PTXMKIMAGE_INPUTPATH",
	}, {
		.name = "outputpath",
		.opt = CFG_STR("outputpath", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_OUTPUTPATH",
	}, {
		.name = "mkfsubifs",
		.opt = CFG_STR("mkfsubifs", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_MKFSUBIFS",
		.def = "mkfs.ubifs",
	}, {
		.name = "mkfsjffs2",
		.opt = CFG_STR("mkfsjffs2", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_MKFJFFS2",
		.def = "mkfs.jffs2",
	}, {
		.name = "ubinize",
		.opt = CFG_STR("ubinize", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_UBINIZE",
		.def = "ubinize",
	}, {
		.name = "genext2fs",
		.opt = CFG_STR("genext2fs", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_GENEXT2FS",
		.def = "genext2fs",
	}, {
		.name = "tar",
		.opt = CFG_STR("tar", NULL, CFGF_NONE),
		.cmdlineopt = 'o',
		.env = "PTXMKIMAGE_TAR",
		.def = "tar",
	},
};

int init_config(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		ret = add_opt(opts[i].name, opts[i].cmdlineopt, opts[i].env, &opts[i].opt, opts[i].def);
		if (ret)
			return ret;
	}

	return 0;
}
