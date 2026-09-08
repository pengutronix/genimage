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
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif
#include <sys/stat.h>

#include "genimage.h"

#define VERITY_SIG_CERT 0
#define VERITY_SIG_KEY	1

static const char *pkcs11_prefix = "pkcs11:";

static char *verity_tmp_path(const char *verity, const char *suffix)
{
	char *path, *slug;

	slug = sanitize_path(verity);
	xasprintf(&path, "%s/%s.%s", tmppath(), slug, suffix);
	free(slug);

	return path;
}

static int verity_sig_write_json_rh(FILE *json, const char *rh)
{
	char line[0x80], *ret;
	FILE *fp;

	fp = fopen(rh, "r");
	if (!fp)
		return -EIO;

	ret = fgets(line, sizeof(line), fp);
	fclose(fp);
	if (!ret)
		return -EIO;

	if (fprintf(json, "\"rootHash\":\"%s\"", line) < 0)
		return -EIO;

	return 0;
}

static int verity_sig_write_json_certfp(FILE *json, const char *certfp)
{
	unsigned char b[32];
	int scanned;
	FILE *fp;

	fp = fopen(certfp, "r");
	if (!fp)
		return -EIO;

	/* clang-format off */
	scanned = fscanf(fp, "sha256 Fingerprint="
			 "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:"
			 "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:"
			 "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:"
			 "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
			 &b[ 0], &b[ 1], &b[ 2], &b[ 3], &b[ 4], &b[ 5], &b[ 6], &b[ 7],
			 &b[ 8], &b[ 9], &b[10], &b[11], &b[12], &b[13], &b[14], &b[15],
			 &b[16], &b[17], &b[18], &b[19], &b[20], &b[21], &b[22], &b[23],
			 &b[24], &b[25], &b[26], &b[27], &b[28], &b[29], &b[30], &b[31]);
	/* clang-format on */
	fclose(fp);
	if (scanned != 32)
		return -EIO;

	/* clang-format off */
	if (fprintf(json, "\"certificateFingerprint\":\""
		    "%02x%02x%02x%02x%02x%02x%02x%02x"
		    "%02x%02x%02x%02x%02x%02x%02x%02x"
		    "%02x%02x%02x%02x%02x%02x%02x%02x"
		    "%02x%02x%02x%02x%02x%02x%02x%02x" "\"",
		    b[ 0], b[ 1], b[ 2], b[ 3], b[ 4], b[ 5], b[ 6], b[ 7],
		    b[ 8], b[ 9], b[10], b[11], b[12], b[13], b[14], b[15],
		    b[16], b[17], b[18], b[19], b[20], b[21], b[22], b[23],
		    b[24], b[25], b[26], b[27], b[28], b[29], b[30], b[31]) < 0)
		return -EIO;
	/* clang-format on */

	return 0;
}

static int verity_sig_write_json_p7s(FILE *json, const char *p7s)
{
	static const char *begin = "-----BEGIN CMS-----";
	static const char *end = "-----END CMS-----";
	char line[0x80], *ret;
	FILE *fp;

	fp = fopen(p7s, "r");
	if (!fp)
		return -EIO;

	ret = fgets(line, sizeof(line), fp);
	if (!ret || strncmp(line, begin, strlen(begin)))
		goto err;

	fputs("\"signature\":\"", json);

	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, end, strlen(end))) {
			fputs("\"", json);
			fclose(fp);
			return 0;
		}

		fwrite(line, 1, strlen(line) - 1, json);
	}

err:
	fclose(fp);
	return -EIO;
}

static ssize_t verity_sig_write_json(struct image *image,
				     const char *rh, const char *certfp, const char *p7s)
{
	ssize_t size;
	FILE *json;
	int ret;

	json = fopen(imageoutfile(image), "w+");
	if (!json) {
		image_error(image, "Unable to open output: %m\n");
		return -errno;
	}

	fputs("{", json);
	ret = verity_sig_write_json_rh(json, rh);
	fputs(",", json);
	ret = ret ? ret : verity_sig_write_json_certfp(json, certfp);
	fputs(",", json);
	ret = ret ? ret : verity_sig_write_json_p7s(json, p7s);
	fputs("}", json);

	if (ret) {
		size = ret;
		goto out;
	}

	size = ftell(json);
	size += 4095;
	size &= ~4095;

	/* UAPI DPS dictates NUL padding up to the next 4k boundary */
	if (ftruncate(fileno(json), size))
		size = ret = -errno;

out:
	fclose(json);

	if (ret)
		image_error(image, "Error while writing output: %m\n");

	return size;
}

static int verity_sig_generate(struct image *image)
{
	char *certfp, *p7s, *rh;
	const char *cert, *key;
	struct partition *part;
	ssize_t size;
	int ret;

	ret = prepare_image(image, image->size);
	if (ret < 0)
		return ret;

	cert = cfg_getstr(image->imagesec, "cert");
	key = cfg_getstr(image->imagesec, "key");

	list_for_each_entry(part, &image->partitions, list) {
		if (part->partition_type == VERITY_SIG_CERT)
			cert = imageoutfile(image_get(part->image));

		if (part->partition_type == VERITY_SIG_KEY)
			key = imageoutfile(image_get(part->image));
	}

	/* The 'image' option points to the 'verity' image for which
	 * we are to generate a signature. During the generation of
	 * that image, the root-hash will have been stored in tmppath
	 * for us to find.
	 */
	rh = verity_tmp_path(cfg_getstr(image->imagesec, "image"), "root-hash");

	p7s = verity_tmp_path(image->file, "p7s");
	certfp = verity_tmp_path(image->file, "certfp");

	ret = systemp(image,
		      "%s cms -sign -noattr -signer '%s' -inkey '%s' "
		      "-binary -in '%s' -outform PEM -out '%s'",
		      get_opt("openssl"), cert, key, rh, p7s);
	if (ret) {
		image_error(image, "Unable to create signature of root-hash\n");
		goto out;
	}

	ret = systemp(image,
		      "%s x509 -fingerprint -sha256 -in '%s' -noout >'%s'",
		      get_opt("openssl"), cert, certfp);
	if (ret) {
		image_error(image, "Unable to extract certificate fingerprint\n");
		goto out;
	}

	size = verity_sig_write_json(image, rh, certfp, p7s);
	if (size < 0) {
		ret = size;
		goto out;
	}

	if (image->size && image->size < (unsigned long)size) {
		image_error(image,
			    "Specified image size (%llu) is too small, generated %ld bytes\n",
			    image->size, size);
		ret = -E2BIG;
		goto out;
	}

	image_debug(image, "generated %ld bytes\n", size);
	image->size = size;

out:
	free(certfp);
	free(p7s);
	free(rh);

	return ret;
}

static int verity_sig_parse(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	const char *cert, *key;

	if (!cfg_getstr(image->imagesec, "image")) {
		image_error(image, "Mandatory 'image' option is missing!\n");
		return -EINVAL;
	}

	cert = cfg_getstr(image->imagesec, "cert");
	if (!cert) {
		image_error(image, "Mandatory 'cert' option is missing!\n");
		return -EINVAL;
	}
	if (strncmp(pkcs11_prefix, cert, strlen(pkcs11_prefix))) {
		part = xzalloc(sizeof(*part));
		part->image = cert;
		part->partition_type = VERITY_SIG_CERT;
		list_add_tail(&part->list, &image->partitions);
	}

	key = cfg_getstr(image->imagesec, "key");
	if (!key) {
		image_error(image, "Mandatory 'key' option is missing!\n");
		return -EINVAL;
	}
	if (strncmp(pkcs11_prefix, key, strlen(pkcs11_prefix))) {
		part = xzalloc(sizeof(*part));
		part->image = key;
		part->partition_type = VERITY_SIG_KEY;
		list_add_tail(&part->list, &image->partitions);
	}
	return 0;
}

static cfg_opt_t verity_sig_opts[] = {
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_STR("cert", NULL, CFGF_NONE),
	CFG_STR("key", NULL, CFGF_NONE),
	CFG_END()
};

struct image_handler verity_sig_handler = {
	.type = "verity-sig",
	.no_rootpath = cfg_true,
	.generate = verity_sig_generate,
	.parse = verity_sig_parse,
	.opts = verity_sig_opts,
};

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
#ifdef __APPLE__
		image_error(image,
			    "Specified image size (%llu) is too small, generated %lld bytes\n",
			    image->size, sb.st_size);
#else
		image_error(image,
			    "Specified image size (%llu) is too small, generated %ld bytes\n",
			    image->size, sb.st_size);
#endif
		return -E2BIG;
	}

#ifdef __APPLE__
	image_debug(image, "generated %lld bytes\n", sb.st_size);
#else
	image_debug(image, "generated %ld bytes\n", sb.st_size);
#endif
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
