/*
 * Copyright (c) 2022 Ahmad Fatoum <a.fatoum@pengutronix.de>
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

static int fip_generate(struct image *image)
{
	struct partition *part;
	char *args = strdup("");
	const char *extraargs = cfg_getstr(image->imagesec, "extraargs");
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child = image_get(part->image);
		char *oldargs;

		oldargs = args;
		xasprintf(&args, "%s --%s '%s'", args, part->name, imageoutfile(child));
		free(oldargs);
	}

	ret = systemp(image, "%s create %s %s '%s'", get_opt("fiptool"),
		      args, extraargs, imageoutfile(image));

	free(args);

	return ret;
}

static void fip_add_part(struct image *image,
			 const char *name, const char *path)
{
	struct partition *part;

	part = xzalloc(sizeof *part);
	part->image = path;
	part->name = name;
	list_add_tail(&part->list, &image->partitions);
}

static cfg_opt_t fip_opts[] = {
	CFG_STR("extraargs",		"", CFGF_NONE),
	CFG_STR_LIST("tos-fw",		NULL, CFGF_NONE),	/* Secure Payload BL32 (Trusted OS, Extra1, Extra 2) */
	/* CFGF_NODEFAULT marks options passed as-is */
	CFG_STR("scp-fwu-cfg",		NULL, CFGF_NODEFAULT),	/* SCP Firmware Updater Configuration FWU SCP_BL2U */
	CFG_STR("ap-fwu-cfg",		NULL, CFGF_NODEFAULT),	/* AP Firmware Updater Configuration BL2U */
	CFG_STR("fwu",			NULL, CFGF_NODEFAULT),	/* Firmware Updater NS_BL2U */
	CFG_STR("fwu-cert",		NULL, CFGF_NODEFAULT),	/* Non-Trusted Firmware Updater certificate */
	CFG_STR("tb-fw",		NULL, CFGF_NODEFAULT),	/* Trusted Boot Firmware BL2 */
	CFG_STR("scp-fw",		NULL, CFGF_NODEFAULT),	/* SCP Firmware SCP_BL2 */
	CFG_STR("soc-fw",		NULL, CFGF_NODEFAULT),	/* EL3 Runtime Firmware BL31 */
	CFG_STR("nt-fw",		NULL, CFGF_NODEFAULT),	/* Non-Trusted Firmware BL33 */
	CFG_STR("fw-config",		NULL, CFGF_NODEFAULT),	/* FW_CONFIG */
	CFG_STR("hw-config",		NULL, CFGF_NODEFAULT),	/* HW_CONFIG */
	CFG_STR("tb-fw-config",		NULL, CFGF_NODEFAULT),	/* TB_FW_CONFIG */
	CFG_STR("soc-fw-config",	NULL, CFGF_NODEFAULT),	/* SOC_FW_CONFIG */
	CFG_STR("tos-fw-config",	NULL, CFGF_NODEFAULT),	/* TOS_FW_CONFIG */
	CFG_STR("nt-fw-config",		NULL, CFGF_NODEFAULT),	/* NT_FW_CONFIG */

	CFG_STR("rot-cert",		NULL, CFGF_NODEFAULT),	/* Root Of Trust key certificate */

	CFG_STR("trusted-key-cert",	NULL, CFGF_NODEFAULT),	/* Trusted key certificate */
	CFG_STR("scp-fw-key-cert",	NULL, CFGF_NODEFAULT),	/* SCP Firmware key certificate */
	CFG_STR("soc-fw-key-cert",	NULL, CFGF_NODEFAULT),	/* SoC Firmware key certificate */
	CFG_STR("tos-fw-key-cert",	NULL, CFGF_NODEFAULT),	/* Trusted OS Firmware key certificate */
	CFG_STR("nt-fw-key-cert",	NULL, CFGF_NODEFAULT),	/* Non-Trusted Firmware key certificate */

	CFG_STR("tb-fw-cert",		NULL, CFGF_NODEFAULT),	/* Trusted Boot Firmware BL2 certificate */
	CFG_STR("scp-fw-cert",		NULL, CFGF_NODEFAULT),	/* SCP Firmware content certificate */
	CFG_STR("soc-fw-cert",		NULL, CFGF_NODEFAULT),	/* SoC Firmware content certificate */
	CFG_STR("tos-fw-cert",		NULL, CFGF_NODEFAULT),	/* Trusted OS Firmware content certificate */
	CFG_STR("nt-fw-cert",		NULL, CFGF_NODEFAULT),	/* Non-Trusted Firmware content certificate */

	CFG_STR("sip-sp-cert",		NULL, CFGF_NODEFAULT),	/* SiP owned Secure Partition content certificate */
	CFG_STR("plat-sp-cert",		NULL, CFGF_NODEFAULT),	/* Platform owned Secure Partition content certificate */

	CFG_END()
};

static const char *tos_fw[] = { "tos-fw", "tos-fw-extra1", "tos-fw-extra2" };

static int fip_parse(struct image *image, cfg_t *cfg)
{
	unsigned int i, num_tos_fw;
	cfg_opt_t *opt;

	num_tos_fw = cfg_size(cfg, "tos-fw");
	if (num_tos_fw > ARRAY_SIZE(tos_fw)) {
		image_error(image, "%u tos-fw binaries given, but maximum is %zu\n",
			    num_tos_fw, ARRAY_SIZE(tos_fw));
		return -EINVAL;
	}

	for (i = 0; i < num_tos_fw; i++)
		fip_add_part(image, tos_fw[i], cfg_getnstr(cfg, "tos-fw", i));

	for (opt = fip_opts; opt->type; opt++) {
		const char *file;

		if (opt->flags != CFGF_NODEFAULT)
			continue;

		file = cfg_getstr(cfg, opt->name);
		if (file)
			fip_add_part(image, opt->name, file);
	}

	return 0;
}

struct image_handler fip_handler = {
	.type = "fip",
	.no_rootpath = cfg_true,
	.generate = fip_generate,
	.parse = fip_parse,
	.opts = fip_opts,
};
