/*
 * Copyright (c) 2025-2026 Tomas Mudrunka <harviecz@gmail.com>
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

/*
 * MDRAID Superblock generator
 * This should create valid mdraid superblock for raid1 with 1 device (more devices can be added once mounted).
 * Unlike mdadm this works completely in userspace and does not need kernel to create the ondisk structures.
 * It is still very basic, but following seems to be working:
 *
 * mdadm --examine test.img
 * losetup /dev/loop1 test.img
 * mdadm --assemble md /dev/loop1
 *
 * Some docs:
 * https://raid.wiki.kernel.org/index.php/RAID_superblock_formats#Sub-versions_of_the_version-1_superblock
 * https://docs.huihoo.com/doxygen/linux/kernel/3.7/md__p_8h_source.html
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <linux/raid/md_p.h>

#include "genimage.h"

/* Max sectors reserved for the write-intent bitmap area */
#define BITMAP_SECTORS_MAX 256
/* (should be divisible by 8 sectors to keep 4kB alignment) */
#define MDRAID_ALIGN_BYTES 8 * 512

/*
 * Array creation timestamp has to be identical across all the raid members,
 * so we share it between invocations
 */
static time_t mdraid_time = 0;

/*
 * bitmap structures:
 * Taken from Linux kernel drivers/md/md-bitmap.h
 * (Currently it's missing from linux-libc-dev debian package, so cannot be simply included)
 */

/* clang-format off */
#ifndef BITMAP_MAGIC
#define BITMAP_MAGIC 0x6d746962 // This is actualy just char string saying "bitm" :-)

/* use these for bitmap->flags and bitmap->sb->state bit-fields */
enum bitmap_state {
	BITMAP_STALE	   = 1,  /* the bitmap file is out of date or had -EIO */
	BITMAP_WRITE_ERROR = 2, /* A write error has occurred */
	BITMAP_HOSTENDIAN  =15,
};

/* the superblock at the front of the bitmap file -- little endian */
typedef struct bitmap_super_s {
	__le32 magic;        /*  0  BITMAP_MAGIC */
	__le32 version;      /*  4  the bitmap major for now, could change... */
	__u8  uuid[16];      /*  8  128 bit uuid - must match md device uuid */
	__le64 events;       /* 24  event counter for the bitmap (1)*/
	__le64 events_cleared;/*32  event counter when last bit cleared (2) */
	__le64 sync_size;    /* 40  the size of the md device's sync range(3) */
	__le32 state;        /* 48  bitmap state information */
	__le32 chunksize;    /* 52  the bitmap chunk size in bytes */
	__le32 daemon_sleep; /* 56  seconds between disk flushes */
	__le32 write_behind; /* 60  number of outstanding write-behind writes */
	__le32 sectors_reserved; /* 64 number of 512-byte sectors that are
				  * reserved for the bitmap. */
	__le32 nodes;        /* 68 the maximum number of nodes in cluster. */
	__u8 cluster_name[64]; /* 72 cluster name to which this md belongs */
	__u8  pad[256 - 136]; /* set to zero */
} bitmap_super_t;
/* clang-format on */

/*
 * notes:
 * (1) This event counter is updated before the eventcounter in the md superblock
 *    When a bitmap is loaded, it is only accepted if this event counter is equal
 *    to, or one greater than, the event counter in the superblock.
 * (2) This event counter is updated when the other one is *if*and*only*if* the
 *    array is not degraded.  As bits are not cleared when the array is degraded,
 *    this represents the last time that any bits were cleared.
 *    If a device is being added that has an event count with this value or
 *    higher, it is accepted as conforming to the bitmap.
 * (3)This is the number of sectors represented by the bitmap, and is the range that
 *    resync happens across.  For raid1 and raid5/6 it is the size of individual
 *    devices.  For raid10 it is the size of the array.
 */
#endif //BITMAP_MAGIC

/* Superblock struct sanity check */
ct_assert(offsetof(struct mdp_superblock_1, data_offset) == 128);
ct_assert(offsetof(struct mdp_superblock_1, utime) == 192);
ct_assert(sizeof(struct mdp_superblock_1) == 256);

/* This structure is used to store mdraid state data in handler_priv */
typedef struct mdraid_img_s {
	/* Partition of data to be imported to raid */
	struct partition img_data_part;
	/* Partition of parent raid image (we can inherit config from it) */
	struct partition img_parent_part;
	/* Images for aforementioned partitions */
	struct image *img_data;
	/* Dtto */
	struct image *img_parent;
	/* Actual mdraid superblock that is gonna be stored on disk */
	struct mdp_superblock_1 *sb;
	size_t superblock_size;
	/* Actual bitmap superblock that is gonna be stored on disk */
	bitmap_super_t bsb;
	/* This is counter used by slave devices to take roles */
	__le16 last_role;
	/* Metadata sub-version string, e.g. "1.0" or "1.2" */
	const char *metadata;
	/* On-disk layout in 512-byte sectors */
	__u64 reserve_sectors;
	__u64 data_offset;
	__u64 data_size;
	__u64 super_offset;
	__s32 bitmap_offset;
	__s32 bblog_offset;
} mdraid_img_t;

/* Calculate on disk layout/offsets based on metadata version */
static int mdraid_calc_layout(struct image *image)
{
	mdraid_img_t *md = image->handler_priv;
	const char *metadata = md->metadata;
	__u64 dev_sectors = image->size / 512;

	if (!metadata || !strcmp(metadata, "1") || !strcmp(metadata, "1.2")) {
		/* metadata 1.2, superblock 4K from start of device - sane default */
		md->metadata = "1.2"; /* ensure we log full metadata version when "1" is entered */
		md->data_offset = 2048;
		md->reserve_sectors = md->data_offset;
		md->super_offset = 8;
		md->bitmap_offset = 8;
		md->bblog_offset = md->bitmap_offset + BITMAP_SECTORS_MAX + 8;
		if (image->size) {
			if (dev_sectors < md->reserve_sectors) {
				image_error(image, "MDRAID image too small for 1.2 metadata.\n");
				return 1;
			}
			md->data_size = dev_sectors - md->reserve_sectors;
		}
	} else if (!strcmp(metadata, "1.0")) {
		/* metadata 1.0, superblock near end of device - needed for UEFI boot  */
		md->reserve_sectors = BITMAP_SECTORS_MAX + 8 + 16;
		md->data_offset = 0;
		md->bitmap_offset = -(__s32)(BITMAP_SECTORS_MAX + 8);
		md->bblog_offset = -8;
		if (image->size) {
			if (dev_sectors < md->reserve_sectors) {
				image_error(image, "MDRAID image too small for 1.0 metadata at end of device.\n");
				return 1;
			}
			md->super_offset = (dev_sectors - 16) & ~7ULL;
			md->data_size = md->super_offset + md->bitmap_offset;
		}
	} else {
		image_error(image, "MDRAID unsupported metadata '%s' (supported: 1.0, 1.2).\n", metadata);
		return 1;
	}

	return 0;
}

static unsigned int calc_sb_1_csum(struct mdp_superblock_1 *sb)
{
	unsigned int disk_csum, csum;
	unsigned long long newcsum;
	int size = sizeof(*sb) + __le32_to_cpu(sb->max_dev) * 2;
	unsigned int *isuper = (unsigned int *)sb;

	/* Temporarily set checksum in struct to 0 while remembering original value */
	disk_csum = sb->sb_csum;
	sb->sb_csum = 0;
	newcsum = 0;
	for (; size >= 4; size -= 4) {
		newcsum += __le32_to_cpu(*isuper);
		isuper++;
	}

	if (size == 2)
		newcsum += __le16_to_cpu(*(unsigned short *)isuper);

	csum = (newcsum & 0xffffffff) + (newcsum >> 32);
	/* Set checksum in struct back to original value */
	sb->sb_csum = disk_csum;
	return __cpu_to_le32(csum);
}

static int mdraid_parse(struct image *image, cfg_t *cfg)
{
	mdraid_img_t *md = xzalloc(sizeof(mdraid_img_t));
	image->handler_priv = md;

	/* Common MDRAID subsystem init */
	if (!mdraid_time)
		mdraid_time = time(NULL);

	/* Sanity checks */
	int raid_level = cfg_getint(image->imagesec, "level");
	if (raid_level != 1) {
		image_error(image, "MDRAID Currently only supporting raid level 1 (mirror)!\n");
		return 1;
	}

	/* Register dependencies only; config/inheritance happens in setup() */
	md->img_parent_part.image = cfg_getstr(image->imagesec, "parent");
	if (md->img_parent_part.image) {
		list_add_tail(&md->img_parent_part.list, &image->partitions);
	} else {
		md->img_data_part.image = cfg_getstr(image->imagesec, "image");
		if (md->img_data_part.image)
			list_add_tail(&md->img_data_part.list, &image->partitions);
	}

	return 0;
}

static int mdraid_setup(struct image *image, cfg_t *cfg)
{
	mdraid_img_t *md = image->handler_priv;
	mdraid_img_t *mdp = NULL;
	__le16 max_devices;
	__le16 role;
	struct mdp_superblock_1 *sb;
	bitmap_super_t *bsb = &md->bsb;
	__le16 *dev_roles;
	char *disk_uuid;
	int i;

	/* Resolve parent and inherit array-wide configuration */
	if (md->img_parent_part.image) {
		md->img_parent = image_get(md->img_parent_part.image);
		if (!md->img_parent) {
			image_error(image, "MDRAID cannot find parent image to inherit metadata config from: %s\n",
				    md->img_parent_part.image);
			return 9;
		}

		mdp = md->img_parent->handler_priv;
		image_info(image, "MDRAID will inherit array metadata config from parent: %s\n",
			   md->img_parent->file);
		image->size = md->img_parent->size;
		md->metadata = mdp->metadata;
		max_devices = mdp->sb->raid_disks;
		/* Data image is set up via the parent dependency chain */
		md->img_data_part.image = cfg_getstr(md->img_parent->imagesec, "image");
	} else {
		md->metadata = cfg_getstr(image->imagesec, "metadata");
		max_devices = cfg_getint(image->imagesec, "devices");
	}

	/* Sets reserve_sectors (and full layout if size is already known) */
	if (mdraid_calc_layout(image))
		return 1;

	image_info(image, "MDRAID using metadata version %s.\n", md->metadata);

	/* Prepare data */
	if (md->img_data_part.image) {
		image_info(image, "MDRAID using data from: %s\n", md->img_data_part.image);
		md->img_data = image_get(md->img_data_part.image);
		if (!md->img_data) {
			image_error(image, "MDRAID cannot get image definition: %s\n", md->img_data_part.image);
			return 8;
		}
		if (image->size == 0)
			image->size = roundup(md->img_data->size + md->reserve_sectors * 512, MDRAID_ALIGN_BYTES);
		if (image->size < (md->img_data->size + md->reserve_sectors * 512)) {
			image_error(image, "MDRAID image too small to fit %s\n", md->img_data->file);
			return 3;
		}
	} else {
		image_info(image, "MDRAID is created without data.\n");
	}

	/* Check alignment */
	if (image->size != roundup(image->size, MDRAID_ALIGN_BYTES)) {
		image_error(image, "MDRAID image size has to be aligned to %d bytes!\n", MDRAID_ALIGN_BYTES);
		return 4;
	}

	/* Recompute size-dependent layout fields now that image->size is final */
	if (mdraid_calc_layout(image))
		return 1;

	/* Determine role of this device in array */
	role = cfg_getint(image->imagesec, "role");
	if (cfg_getint(image->imagesec, "role") == -1) {
		if (mdp)
			role = ++mdp->last_role;
		else
			role = 0;
		image_info(image, "MDRAID automaticaly assigned role %d.\n", role);
	}

	if (role > MD_DISK_ROLE_MAX) {
		image_error(image, "MDRAID role has to be >= 0 and <= %d.\n", MD_DISK_ROLE_MAX);
		return 6;
	}

	if (role >= max_devices) {
		image_error(image, "MDRAID role of this image has to be lower than total number of %d devices (roles are counted from 0).\n",
			    max_devices);
		return 5;
	}

	/* Build MD superblock and bitmap superblock */
	md->superblock_size = sizeof(struct mdp_superblock_1) + max_devices * 2;
	sb = md->sb = xzalloc(md->superblock_size);

	if (mdp) {
		memcpy(md->sb, mdp->sb, md->superblock_size);
	} else {
		char *name = cfg_getstr(image->imagesec, "label");
		char *raid_uuid = cfg_getstr(image->imagesec, "raid-uuid");
		long int timestamp = cfg_getint(image->imagesec, "timestamp");

		/* constant array information - 128 bytes */
		sb->magic = MD_SB_MAGIC;
		sb->major_version = 1;
		sb->feature_map = MD_FEATURE_BITMAP_OFFSET;
		sb->pad0 = 0;

		if (!raid_uuid)
			raid_uuid = uuid_random();
		uuid_parse(raid_uuid, sb->set_uuid);

		strncpy(sb->set_name, name, 32);
		sb->set_name[31] = 0;

		if (timestamp >= 0)
			sb->ctime = timestamp & 0xffffffffff;
		else
			sb->ctime = mdraid_time & 0xffffffffff;

		sb->level = 1;
		sb->chunksize = 0;
		sb->raid_disks = max_devices;
		sb->size = md->data_size;
	}

	sb->bitmap_offset = __cpu_to_le32((unsigned)md->bitmap_offset);
	sb->data_offset = md->data_offset;
	sb->data_size = md->data_size;
	sb->super_offset = md->super_offset;
	sb->dev_number = role;
	sb->cnt_corrected_read = 0;

	disk_uuid = cfg_getstr(image->imagesec, "disk-uuid");
	if (!disk_uuid)
		disk_uuid = uuid_random();
	uuid_parse(disk_uuid, sb->device_uuid);

	sb->devflags = 0;
	sb->bblog_shift = 9;
	sb->bblog_size = 8;
	sb->bblog_offset = __cpu_to_le32((unsigned)md->bblog_offset);

	sb->utime = sb->ctime;
	sb->events = 0;
	sb->resync_offset = 0;
	sb->max_dev = max_devices;

	dev_roles = (__le16 *)((char *)sb + sizeof(struct mdp_superblock_1));
	for (i = 0; i < max_devices; i++)
		dev_roles[i] = i;

	sb->sb_csum = calc_sb_1_csum(sb);

	bsb->magic = BITMAP_MAGIC;
	bsb->version = 4; /* v4 is compatible with mdraid v1.x */
	memcpy(bsb->uuid, sb->set_uuid, sizeof(bsb->uuid));
	bsb->events = 0;
	bsb->events_cleared = 0;
	bsb->sync_size = sb->data_size;
	bsb->state = 0;
	bsb->chunksize = 64 * 1024 * 1024;
	bsb->daemon_sleep = 5;
	bsb->write_behind = 0;
	bsb->sectors_reserved = roundup(bsb->sync_size / bsb->chunksize, 8);
	bsb->nodes = 0;

	while (bsb->sectors_reserved > BITMAP_SECTORS_MAX) {
		bsb->chunksize *= 2;
		bsb->sectors_reserved = roundup(bsb->sync_size / bsb->chunksize, 8);
	}

	return 0;
}

static int mdraid_generate(struct image *image)
{
	mdraid_img_t *md = image->handler_priv;
	struct mdp_superblock_1 *sb = md->sb;
	bitmap_super_t *bsb = &md->bsb;
	int ret;

	/* create empty image */
	ret = prepare_image(image, image->size);
	if (ret)
		return ret;

	/* insert superblock */
	ret = insert_data(image, sb, imageoutfile(image), md->superblock_size, sb->super_offset * 512);
	if (ret)
		return ret;

	/* insert bitmap */
	if (sb->feature_map & MD_FEATURE_BITMAP_OFFSET) {
		__s64 bitmap_sector = (__s64)sb->super_offset +
				      (__s32)__le32_to_cpu(sb->bitmap_offset);

		ret = insert_data(image, bsb, imageoutfile(image), sizeof(*bsb),
				  (unsigned long long)bitmap_sector * 512);
		if (ret)
			return ret;
	}

	/* insert data */
	if (md->img_data) {
		ret = insert_image(image, md->img_data, md->img_data->size,
				   md->data_offset * 512, 0, 0, cfg_true);
		if (ret)
			return ret;
	}

	return 0;
}

static cfg_opt_t mdraid_opts[] = {
	CFG_STR("label", "any:42", CFGF_NONE),
	CFG_INT("level", 1, CFGF_NONE),
	CFG_INT("devices", 1, CFGF_NONE),
	CFG_INT("role", -1, CFGF_NONE),
	CFG_INT("timestamp", -1, CFGF_NONE),
	CFG_STR("raid-uuid", NULL, CFGF_NONE),
	CFG_STR("disk-uuid", NULL, CFGF_NONE),
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_STR("parent", NULL, CFGF_NONE),
	CFG_STR("metadata", "1.2", CFGF_NONE),
	CFG_END()
};

struct image_handler mdraid_handler = {
	.type = "mdraid",
	.no_rootpath = cfg_true,
	.parse = mdraid_parse,
	.setup = mdraid_setup,
	.generate = mdraid_generate,
	.opts = mdraid_opts,
};
