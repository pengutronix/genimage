/*
 * Copyright (c) 2016 Michael Olbrich <m.olbrich@pengutronix.de>
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

#define RAUC_CONTENT	0
#define RAUC_KEY	1
#define RAUC_CERT	2
#define RAUC_KEYRING	3

static int rauc_generate(struct image *image)
{
	int ret;
	struct partition *part;
	char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	char *manifest = cfg_getstr(image->imagesec, "manifest");
	const char *cert = cfg_getstr(image->imagesec, "cert");
	const char *key = cfg_getstr(image->imagesec, "key");
	const char *keyring = cfg_getstr(image->imagesec, "keyring");
	char *keyringarg = "";
	char *manifest_file, *tmpdir;

	image_debug(image, "manifest = '%s'\n", manifest);

	xasprintf(&tmpdir, "%s/rauc-%s", tmppath(), sanitize_path(image->file));
	ret = systemp(image, "mkdir -p '%s'", tmpdir);
	if (ret)
		return ret;

	xasprintf(&manifest_file, "%s/manifest.raucm", tmpdir);
	ret = insert_data(image, manifest, manifest_file, strlen(manifest), 0);
	if (ret)
		return ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child = image_get(part->image);
		const char *file = imageoutfile(child);
		const char *target = part->name;
		char *path, *tmp;

		if (part->partition_type == RAUC_CERT)
			cert = file;

		if (part->partition_type == RAUC_KEY)
			key = file;

		if (part->partition_type == RAUC_KEYRING)
			keyring = file;

		if (part->partition_type != RAUC_CONTENT)
			continue;

		if (!target) {
			/* use basename from source as target name */
			tmp = strrchr(child->file, '/');
			if (tmp)
				target = tmp + 1;
			else
				target = child->file;
		}

		/* create parent directories if target needs it */
		path = strdupa(target);
		tmp = strrchr(path, '/');
		if (tmp) {
			*tmp = '\0';
			ret = systemp(image, "mkdir -p '%s/%s'",
					tmpdir, path);
			if (ret)
				return ret;
		}

		image_info(image, "adding file '%s' as '%s' ...\n",
				child->file, target);
		ret = systemp(image, "cp --remove-destination '%s' '%s/%s'",
				file, tmpdir, target);
		if (ret)
			return ret;
	}

	if (keyring)
		xasprintf(&keyringarg, "--keyring='%s'", keyring);

	systemp(image, "rm -f '%s'", imageoutfile(image));

	ret = systemp(image, "%s bundle '%s' --cert='%s' --key='%s' %s %s '%s'",
			get_opt("rauc"), tmpdir, cert, key,
			keyringarg, extraargs, imageoutfile(image));

	return ret;
}

static int rauc_parse(struct image *image, cfg_t *cfg)
{
	const char *pkcs11_prefix = "pkcs11:";
	unsigned int i;
	unsigned int num_files;
	struct partition *part;
	char *part_image_key;
	char *part_image_cert;
	char *part_image_keyring;

	part_image_key = cfg_getstr(image->imagesec, "key");
	if (!part_image_key) {
		image_error(image, "Mandatory 'key' option is missing!\n");
		return -EINVAL;
	}
	if (strncmp(pkcs11_prefix, part_image_key, strlen(pkcs11_prefix))) {
		part = xzalloc(sizeof *part);
		part->image = part_image_key;
		part->partition_type = RAUC_KEY;
		list_add_tail(&part->list, &image->partitions);
	}

	part_image_cert = cfg_getstr(image->imagesec, "cert");
	if (!part_image_cert) {
		image_error(image, "Mandatory 'cert' option is missing!\n");
		return -EINVAL;
	}
	if (strncmp(pkcs11_prefix, part_image_cert, strlen(pkcs11_prefix))) {
		part = xzalloc(sizeof *part);
		part->image = part_image_cert;
		part->partition_type = RAUC_CERT;
		list_add_tail(&part->list, &image->partitions);
	}

	part_image_keyring = cfg_getstr(image->imagesec, "keyring");
	if (part_image_keyring) {
		part = xzalloc(sizeof *part);
		part->image = part_image_keyring;
		part->partition_type = RAUC_KEYRING;
		list_add_tail(&part->list, &image->partitions);
	}

	num_files = cfg_size(cfg, "file");
	for (i = 0; i < num_files; i++) {
		cfg_t *filesec = cfg_getnsec(cfg, "file", i);
		part = xzalloc(sizeof *part);
		part->name = cfg_title(filesec);
		part->image = cfg_getstr(filesec, "image");
		part->partition_type = RAUC_CONTENT;
		list_add_tail(&part->list, &image->partitions);
	}

	for(i = 0; i < cfg_size(cfg, "files"); i++) {
		part = xzalloc(sizeof *part);
		part->image = cfg_getnstr(cfg, "files", i);
		part->partition_type = RAUC_CONTENT;
		list_add_tail(&part->list, &image->partitions);
	}

	return 0;
}

static int rauc_setup(struct image *image, cfg_t *cfg)
{
	char *manifest = cfg_getstr(image->imagesec, "manifest");
	if (!manifest) {
		image_error(image, "Mandatory 'manifest' option is missing!\n");
		return -EINVAL;
	}
	return 0;
}

static cfg_opt_t file_opts[] = {
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_END()
};

static cfg_opt_t rauc_opts[] = {
	CFG_STR("extraargs", "", CFGF_NONE),
	CFG_STR_LIST("files", 0, CFGF_NONE),
	CFG_SEC("file", file_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("key", NULL, CFGF_NONE),
	CFG_STR("cert", NULL, CFGF_NONE),
	CFG_STR("keyring", NULL, CFGF_NONE),
	CFG_STR("manifest", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler rauc_handler = {
	.type = "rauc",
	.generate = rauc_generate,
	.parse = rauc_parse,
	.setup = rauc_setup,
	.opts = rauc_opts,
};
