/*
 * Copyright (c) 2011 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *           (c) 2011 Michael Olbrich <m.olbrich@pengutronix.de>
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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <endian.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "genimage.h"

#define TYPE_NONE   0
#define TYPE_MBR    1
#define TYPE_GPT    2
#define TYPE_HYBRID (TYPE_MBR|TYPE_GPT)

#define PARTITION_TYPE_EXTENDED 0x0F

struct hdimage {
	unsigned int extended_partition_index;
	struct partition *extended_partition;
	unsigned long long align;
	uint32_t disksig;
	const char *disk_uuid;
	int table_type;
	unsigned long long gpt_location;
	cfg_bool_t gpt_no_backup;
	cfg_bool_t fill;
	unsigned long long file_size;
};

struct mbr_partition_entry {
	unsigned char boot;

	unsigned char first_chs[3];

	unsigned char partition_type;

	unsigned char last_chs[3];

	uint32_t relative_sectors;
	uint32_t total_sectors;
} __attribute__((packed));
ct_assert(sizeof(struct mbr_partition_entry) == 16);

struct mbr_tail {
	uint32_t disk_signature;
	uint16_t copy_protect;
	struct mbr_partition_entry part_entry[4];
	uint16_t boot_signature;
} __attribute__((packed));
ct_assert(sizeof(struct mbr_tail) == 72);

struct gpt_header {
	unsigned char signature[8];
	uint32_t revision;
	uint32_t header_size;
	uint32_t header_crc;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	unsigned char disk_uuid[16];
	uint64_t starting_lba;
	uint32_t number_entries;
	uint32_t entry_size;
	uint32_t table_crc;
} __attribute__((packed));
ct_assert(sizeof(struct gpt_header) == 92);

struct gpt_partition_entry {
	unsigned char type_uuid[16];
	unsigned char uuid[16];
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t flags;
	uint16_t name[36];
} __attribute__((packed));
ct_assert(sizeof(struct gpt_partition_entry) == 128);

#define GPT_ENTRIES 		128
#define GPT_SECTORS		(1 + GPT_ENTRIES * sizeof(struct gpt_partition_entry) / 512)
#define GPT_REVISION_1_0	0x00010000

#define GPT_PE_FLAG_BOOTABLE	(1ULL << 2)
#define GPT_PE_FLAG_READ_ONLY	(1ULL << 60)
#define GPT_PE_FLAG_HIDDEN	(1ULL << 62)
#define GPT_PE_FLAG_NO_AUTO	(1ULL << 63)

static unsigned long long partition_end(const struct partition *part)
{
	return part->offset + part->size;
}

static void lba_to_chs(unsigned int lba, unsigned char *chs)
{
	const unsigned int hpc = 255;
	const unsigned int spt = 63;
	unsigned int s, c;

	chs[0] = (lba/spt)%hpc;
	c = (lba/(spt * hpc));
	s = (lba > 0) ?(lba%spt + 1) : 0;
	chs[1] = ((c & 0x300) >> 2) | (s & 0xff);
	chs[2] = (c & 0xff);
}

static void hdimage_setup_chs(struct mbr_partition_entry *entry,
			      unsigned long long offset_sectors)
{
	lba_to_chs(entry->relative_sectors + offset_sectors,
		   entry->first_chs);
	lba_to_chs(entry->relative_sectors + entry->total_sectors - 1 + offset_sectors,
		   entry->last_chs);
}

static int hdimage_insert_mbr(struct image *image, struct list_head *partitions)
{
	struct hdimage *hd = image->handler_priv;
	struct mbr_tail mbr;
	struct partition *part;
	int ret, i = 0;

	if (hd->table_type == TYPE_HYBRID) {
		image_info(image, "writing hybrid MBR\n");
	} else {
		image_info(image, "writing MBR\n");
	}

	memset(&mbr, 0, sizeof(mbr));
	memcpy(&mbr.disk_signature, &hd->disksig, sizeof(hd->disksig));

	list_for_each_entry(part, partitions, list) {
		struct mbr_partition_entry *entry;

		if (!part->in_partition_table || part->logical)
			continue;

		if (hd->table_type == TYPE_HYBRID && !part->partition_type)
			continue;

		entry = &mbr.part_entry[i];

		entry->boot = part->bootable ? 0x80 : 0x00;
		entry->partition_type = part->partition_type;
		entry->relative_sectors = part->offset/512;
		entry->total_sectors = part->size/512;
		hdimage_setup_chs(entry, 0);

		image_debug(image, "[MBR entry %d]: type=%x start=%d size=%d\n",
					i, entry->partition_type,
					entry->relative_sectors, entry->total_sectors);

		i++;
	}

	if (hd->table_type == TYPE_HYBRID) {
		struct mbr_partition_entry *entry;

		entry = &mbr.part_entry[i];

		entry->boot = 0x00;

		entry->partition_type = 0xee;
		entry->relative_sectors = 1;
		entry->total_sectors = hd->gpt_location / 512 + GPT_SECTORS - 2;

		hdimage_setup_chs(entry, 0);
	}

	mbr.boot_signature = htole16(0xaa55);

	ret = insert_data(image, &mbr, imageoutfile(image), sizeof(mbr), 440);
	if (ret) {
		if (hd->table_type == TYPE_HYBRID) {
			image_error(image, "failed to write hybrid MBR\n");
		} else {
			image_error(image, "failed to write MBR\n");
		}
		return ret;
	}

	return 0;
}

static int hdimage_insert_ebr(struct image *image, struct partition *part)
{
	struct hdimage *hd = image->handler_priv;
	struct mbr_partition_entry *entry;
	char ebr[4*sizeof(struct mbr_partition_entry)+2], *part_table;
	int ret;
	unsigned long long ebr_offset = part->offset - hd->align + 446;

	image_debug(image, "writing EBR to sector %llu\n", ebr_offset / 512);

	memset(ebr, 0, sizeof(ebr));
	part_table = ebr;
	entry = (struct mbr_partition_entry *)part_table;

	entry->boot = 0x00;
	entry->partition_type = part->partition_type;
	entry->relative_sectors = hd->align/512;
	entry->total_sectors = part->size/512;
	// absolute CHS address of the logical partition
	// equals to absolute partition offset
	hdimage_setup_chs(entry, (part->offset - hd->align) / 512);
	struct partition *p = part;
	list_for_each_entry_continue(p, &image->partitions, list) {
		if (!p->logical)
			continue;
		++entry;
		entry->boot = 0x00;
		entry->partition_type = PARTITION_TYPE_EXTENDED;
		entry->relative_sectors = (p->offset - hd->align - hd->extended_partition->offset)/512;
		entry->total_sectors = (p->size + hd->align)/512;
		// absolute CHS address of the next EBR
		// equals to relative address within extended partition + partition start
		hdimage_setup_chs(entry, hd->extended_partition->offset / 512);
		break;
	}

	part_table += 4 * sizeof(struct mbr_partition_entry);
	part_table[0] = 0x55;
	part_table[1] = 0xaa;

	ret = insert_data(image, ebr, imageoutfile(image), sizeof(ebr),
			  ebr_offset);
	if (ret) {
		image_error(image, "failed to write EBR\n");
		return ret;
	}

	return 0;
}

struct gpt_partition_type_shortcut_t
{
	const char * shortcut;
	const char * guid;
};

static const struct gpt_partition_type_shortcut_t gpt_partition_type_shortcuts[] =
{
	{ "L"                           , "0fc63daf-8483-4772-8e79-3d69d8477de4" },
	{ "linux"                       , "0fc63daf-8483-4772-8e79-3d69d8477de4" },
	{ "S"                           , "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f" },
	{ "swap"                        , "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f" },
	{ "H"                           , "933ac7e1-2eb4-4f13-b844-0e14e2aef915" },
	{ "home"                        , "933ac7e1-2eb4-4f13-b844-0e14e2aef915" },
	{ "U"                           , "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" },
	{ "uefi"                        , "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" },
	{ "R"                           , "a19d880f-05fc-4d3b-a006-743f0f84911e" },
	{ "raid"                        , "a19d880f-05fc-4d3b-a006-743f0f84911e" },
	{ "V"                           , "e6d6d379-f507-44c2-a23c-238f2a3df928" },
	{ "lvm"                         , "e6d6d379-f507-44c2-a23c-238f2a3df928" },
	{ "F"                           , "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7" },
	{ "fat32"                       , "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7" },
	{ "barebox-state"               , "4778ed65-bf42-45fa-9c5b-287a1dc4aab1" },
	{ "barebox-env"                 , "6c3737f2-07f8-45d1-ad45-15d260aab24d" },
	/* Discoverable Partitions Specification GUID, see
	 * https://uapi-group.org/specifications/specs/discoverable_partitions_specification/
	 */
	{ "root-alpha"                  , "6523f8ae-3eb1-4e2a-a05a-18b695ae656f" },
	{ "root-arc"                    , "d27f46ed-2919-4cb8-bd25-9531f3c16534" },
	{ "root-arm"                    , "69dad710-2ce4-4e3c-b16c-21a1d49abed3" },
	{ "root-arm64"                  , "b921b045-1df0-41c3-af44-4c6f280d3fae" },
	{ "root-ia64"                   , "993d8d3d-f80e-4225-855a-9daf8ed7ea97" },
	{ "root-loongarch64"            , "77055800-792c-4f94-b39a-98c91b762bb6" },
	{ "root-mips"                   , "e9434544-6e2c-47cc-bae2-12d6deafb44c" },
	{ "root-mips64"                 , "d113af76-80ef-41b4-bdb6-0cff4d3d4a25" },
	{ "root-mips-le"                , "37c58c8a-d913-4156-a25f-48b1b64e07f0" },
	{ "root-mips64-le"              , "700bda43-7a34-4507-b179-eeb93d7a7ca3" },
	{ "root-parisc"                 , "1aacdb3b-5444-4138-bd9e-e5c2239b2346" },
	{ "root-ppc"                    , "1de3f1ef-fa98-47b5-8dcd-4a860a654d78" },
	{ "root-ppc64"                  , "912ade1d-a839-4913-8964-a10eee08fbd2" },
	{ "root-ppc64-le"               , "c31c45e6-3f39-412e-80fb-4809c4980599" },
	{ "root-riscv32"                , "60d5a7fe-8e7d-435c-b714-3dd8162144e1" },
	{ "root-riscv64"                , "72ec70a6-cf74-40e6-bd49-4bda08e8f224" },
	{ "root-s390"                   , "08a7acea-624c-4a20-91e8-6e0fa67d23f9" },
	{ "root-s390x"                  , "5eead9a9-fe09-4a1e-a1d7-520d00531306" },
	{ "root-tilegx"                 , "c50cdd70-3862-4cc3-90e1-809a8c93ee2c" },
	{ "root-x86"                    , "44479540-f297-41b2-9af7-d131d5f0458a" },
	{ "root-x86-64"                 , "4f68bce3-e8cd-4db1-96e7-fbcaf984b709" },
	{ "usr-alpha"                   , "e18cf08c-33ec-4c0d-8246-c6c6fb3da024" },
	{ "usr-arc"                     , "7978a683-6316-4922-bbee-38bff5a2fecc" },
	{ "usr-arm"                     , "7d0359a3-02b3-4f0a-865c-654403e70625" },
	{ "usr-arm64"                   , "b0e01050-ee5f-4390-949a-9101b17104e9" },
	{ "usr-ia64"                    , "4301d2a6-4e3b-4b2a-bb94-9e0b2c4225ea" },
	{ "usr-loongarch64"             , "e611c702-575c-4cbe-9a46-434fa0bf7e3f" },
	{ "usr-mips"                    , "773b2abc-2a99-4398-8bf5-03baac40d02b" },
	{ "usr-mips64"                  , "57e13958-7331-4365-8e6e-35eeee17c61b" },
	{ "usr-mips-le"                 , "0f4868e9-9952-4706-979f-3ed3a473e947" },
	{ "usr-mips64-le"               , "c97c1f32-ba06-40b4-9f22-236061b08aa8" },
	{ "usr-parisc"                  , "dc4a4480-6917-4262-a4ec-db9384949f25" },
	{ "usr-ppc"                     , "7d14fec5-cc71-415d-9d6c-06bf0b3c3eaf" },
	{ "usr-ppc64"                   , "2c9739e2-f068-46b3-9fd0-01c5a9afbcca" },
	{ "usr-ppc64-le"                , "15bb03af-77e7-4d4a-b12b-c0d084f7491c" },
	{ "usr-riscv32"                 , "b933fb22-5c3f-4f91-af90-e2bb0fa50702" },
	{ "usr-riscv64"                 , "beaec34b-8442-439b-a40b-984381ed097d" },
	{ "usr-s390"                    , "cd0f869b-d0fb-4ca0-b141-9ea87cc78d66" },
	{ "usr-s390x"                   , "8a4f5770-50aa-4ed3-874a-99b710db6fea" },
	{ "usr-tilegx"                  , "55497029-c7c1-44cc-aa39-815ed1558630" },
	{ "usr-x86"                     , "75250d76-8cc6-458e-bd66-bd47cc81a812" },
	{ "usr-x86-64"                  , "8484680c-9521-48c6-9c11-b0720656f69e" },
	{ "root-alpha-verity"           , "fc56d9e9-e6e5-4c06-be32-e74407ce09a5" },
	{ "root-arc-verity"             , "24b2d975-0f97-4521-afa1-cd531e421b8d" },
	{ "root-arm-verity"             , "7386cdf2-203c-47a9-a498-f2ecce45a2d6" },
	{ "root-arm64-verity"           , "df3300ce-d69f-4c92-978c-9bfb0f38d820" },
	{ "root-ia64-verity"            , "86ed10d5-b607-45bb-8957-d350f23d0571" },
	{ "root-loongarch64-verity"     , "f3393b22-e9af-4613-a948-9d3bfbd0c535" },
	{ "root-mips-verity"            , "7a430799-f711-4c7e-8e5b-1d685bd48607" },
	{ "root-mips64-verity"          , "579536f8-6a33-4055-a95a-df2d5e2c42a8" },
	{ "root-mips-le-verity"         , "d7d150d2-2a04-4a33-8f12-16651205ff7b" },
	{ "root-mips64-le-verity"       , "16b417f8-3e06-4f57-8dd2-9b5232f41aa6" },
	{ "root-parisc-verity"          , "d212a430-fbc5-49f9-a983-a7feef2b8d0e" },
	{ "root-ppc64-le-verity"        , "906bd944-4589-4aae-a4e4-dd983917446a" },
	{ "root-ppc64-verity"           , "9225a9a3-3c19-4d89-b4f6-eeff88f17631" },
	{ "root-ppc-verity"             , "98cfe649-1588-46dc-b2f0-add147424925" },
	{ "root-riscv32-verity"         , "ae0253be-1167-4007-ac68-43926c14c5de" },
	{ "root-riscv64-verity"         , "b6ed5582-440b-4209-b8da-5ff7c419ea3d" },
	{ "root-s390-verity"            , "7ac63b47-b25c-463b-8df8-b4a94e6c90e1" },
	{ "root-s390x-verity"           , "b325bfbe-c7be-4ab8-8357-139e652d2f6b" },
	{ "root-tilegx-verity"          , "966061ec-28e4-4b2e-b4a5-1f0a825a1d84" },
	{ "root-x86-64-verity"          , "2c7357ed-ebd2-46d9-aec1-23d437ec2bf5" },
	{ "root-x86-verity"             , "d13c5d3b-b5d1-422a-b29f-9454fdc89d76" },
	{ "usr-alpha-verity"            , "8cce0d25-c0d0-4a44-bd87-46331bf1df67" },
	{ "usr-arc-verity"              , "fca0598c-d880-4591-8c16-4eda05c7347c" },
	{ "usr-arm-verity"              , "c215d751-7bcd-4649-be90-6627490a4c05" },
	{ "usr-arm64-verity"            , "6e11a4e7-fbca-4ded-b9e9-e1a512bb664e" },
	{ "usr-ia64-verity"             , "6a491e03-3be7-4545-8e38-83320e0ea880" },
	{ "usr-loongarch64-verity"      , "f46b2c26-59ae-48f0-9106-c50ed47f673d" },
	{ "usr-mips-verity"             , "6e5a1bc8-d223-49b7-bca8-37a5fcceb996" },
	{ "usr-mips64-verity"           , "81cf9d90-7458-4df4-8dcf-c8a3a404f09b" },
	{ "usr-mips-le-verity"          , "46b98d8d-b55c-4e8f-aab3-37fca7f80752" },
	{ "usr-mips64-le-verity"        , "3c3d61fe-b5f3-414d-bb71-8739a694a4ef" },
	{ "usr-parisc-verity"           , "5843d618-ec37-48d7-9f12-cea8e08768b2" },
	{ "usr-ppc64-le-verity"         , "ee2b9983-21e8-4153-86d9-b6901a54d1ce" },
	{ "usr-ppc64-verity"            , "bdb528a5-a259-475f-a87d-da53fa736a07" },
	{ "usr-ppc-verity"              , "df765d00-270e-49e5-bc75-f47bb2118b09" },
	{ "usr-riscv32-verity"          , "cb1ee4e3-8cd0-4136-a0a4-aa61a32e8730" },
	{ "usr-riscv64-verity"          , "8f1056be-9b05-47c4-81d6-be53128e5b54" },
	{ "usr-s390-verity"             , "b663c618-e7bc-4d6d-90aa-11b756bb1797" },
	{ "usr-s390x-verity"            , "31741cc4-1a2a-4111-a581-e00b447d2d06" },
	{ "usr-tilegx-verity"           , "2fb4bf56-07fa-42da-8132-6b139f2026ae" },
	{ "usr-x86-64-verity"           , "77ff5f63-e7b6-4633-acf4-1565b864c0e6" },
	{ "usr-x86-verity"              , "8f461b0d-14ee-4e81-9aa9-049b6fb97abd" },
	{ "root-alpha-verity-sig"       , "d46495b7-a053-414f-80f7-700c99921ef8" },
	{ "root-arc-verity-sig"         , "143a70ba-cbd3-4f06-919f-6c05683a78bc" },
	{ "root-arm-verity-sig"         , "42b0455f-eb11-491d-98d3-56145ba9d037" },
	{ "root-arm64-verity-sig"       , "6db69de6-29f4-4758-a7a5-962190f00ce3" },
	{ "root-ia64-verity-sig"        , "e98b36ee-32ba-4882-9b12-0ce14655f46a" },
	{ "root-loongarch64-verity-sig" , "5afb67eb-ecc8-4f85-ae8e-ac1e7c50e7d0" },
	{ "root-mips-verity-sig"        , "bba210a2-9c5d-45ee-9e87-ff2ccbd002d0" },
	{ "root-mips64-verity-sig"      , "43ce94d4-0f3d-4999-8250-b9deafd98e6e" },
	{ "root-mips-le-verity-sig"     , "c919cc1f-4456-4eff-918c-f75e94525ca5" },
	{ "root-mips64-le-verity-sig"   , "904e58ef-5c65-4a31-9c57-6af5fc7c5de7" },
	{ "root-parisc-verity-sig"      , "15de6170-65d3-431c-916e-b0dcd8393f25" },
	{ "root-ppc64-le-verity-sig"    , "d4a236e7-e873-4c07-bf1d-bf6cf7f1c3c6" },
	{ "root-ppc64-verity-sig"       , "f5e2c20c-45b2-4ffa-bce9-2a60737e1aaf" },
	{ "root-ppc-verity-sig"         , "1b31b5aa-add9-463a-b2ed-bd467fc857e7" },
	{ "root-riscv32-verity-sig"     , "3a112a75-8729-4380-b4cf-764d79934448" },
	{ "root-riscv64-verity-sig"     , "efe0f087-ea8d-4469-821a-4c2a96a8386a" },
	{ "root-s390-verity-sig"        , "3482388e-4254-435a-a241-766a065f9960" },
	{ "root-s390x-verity-sig"       , "c80187a5-73a3-491a-901a-017c3fa953e9" },
	{ "root-tilegx-verity-sig"      , "b3671439-97b0-4a53-90f7-2d5a8f3ad47b" },
	{ "root-x86-64-verity-sig"      , "41092b05-9fc8-4523-994f-2def0408b176" },
	{ "root-x86-verity-sig"         , "5996fc05-109c-48de-808b-23fa0830b676" },
	{ "usr-alpha-verity-sig"        , "5c6e1c76-076a-457a-a0fe-f3b4cd21ce6e" },
	{ "usr-arc-verity-sig"          , "94f9a9a1-9971-427a-a400-50cb297f0f35" },
	{ "usr-arm-verity-sig"          , "d7ff812f-37d1-4902-a810-d76ba57b975a" },
	{ "usr-arm64-verity-sig"        , "c23ce4ff-44bd-4b00-b2d4-b41b3419e02a" },
	{ "usr-ia64-verity-sig"         , "8de58bc2-2a43-460d-b14e-a76e4a17b47f" },
	{ "usr-loongarch64-verity-sig"  , "b024f315-d330-444c-8461-44bbde524e99" },
	{ "usr-mips-verity-sig"         , "97ae158d-f216-497b-8057-f7f905770f54" },
	{ "usr-mips64-verity-sig"       , "05816ce2-dd40-4ac6-a61d-37d32dc1ba7d" },
	{ "usr-mips-le-verity-sig"      , "3e23ca0b-a4bc-4b4e-8087-5ab6a26aa8a9" },
	{ "usr-mips64-le-verity-sig"    , "f2c2c7ee-adcc-4351-b5c6-ee9816b66e16" },
	{ "usr-parisc-verity-sig"       , "450dd7d1-3224-45ec-9cf2-a43a346d71ee" },
	{ "usr-ppc64-le-verity-sig"     , "c8bfbd1e-268e-4521-8bba-bf314c399557" },
	{ "usr-ppc64-verity-sig"        , "0b888863-d7f8-4d9e-9766-239fce4d58af" },
	{ "usr-ppc-verity-sig"          , "7007891d-d371-4a80-86a4-5cb875b9302e" },
	{ "usr-riscv32-verity-sig"      , "c3836a13-3137-45ba-b583-b16c50fe5eb4" },
	{ "usr-riscv64-verity-sig"      , "d2f9000a-7a18-453f-b5cd-4d32f77a7b32" },
	{ "usr-s390-verity-sig"         , "17440e4f-a8d0-467f-a46e-3912ae6ef2c5" },
	{ "usr-s390x-verity-sig"        , "3f324816-667b-46ae-86ee-9b0c0c6c11b4" },
	{ "usr-tilegx-verity-sig"       , "4ede75e2-6ccc-4cc8-b9c7-70334b087510" },
	{ "usr-x86-64-verity-sig"       , "e7bb33fb-06cf-4e81-8273-e543b413e2e2" },
	{ "usr-x86-verity-sig"          , "974a71c0-de41-43c3-be5d-5c5ccd1ad2c0" },
	{ "esp"                         , "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" },
	{ "xbootldr"                    , "bc13c2ff-59e6-4262-a352-b275fd6f7172" },
	{ "srv"                         , "3b8f8425-20e0-4f3b-907f-1a25a76f98e8" },
	{ "var"                         , "4d21b016-b534-45c2-a9fb-5c16e091fd2d" },
	{ "tmp"                         , "7ec6f557-3bc5-4aca-b293-16ef5df639d1" },
	{ "user-home"                   , "773f91ef-66d4-49b5-bd83-d683bf40ad16" },
	{ "linux-generic"               , "0fc63daf-8483-4772-8e79-3d69d8477de4" },

	{ 0, 0 } /* sentinel */
};

static const char *
gpt_partition_type_lookup(const char * shortcut)
{
	const struct gpt_partition_type_shortcut_t * s;
	for(s = gpt_partition_type_shortcuts; s->shortcut; s++) {
		if(strcasecmp(s->shortcut, shortcut) == 0) {
			return s->guid;
		}
	}

	return NULL;
}


static int hdimage_insert_protective_mbr(struct image *image)
{
	struct partition mbr;
	struct list_head mbr_list = LIST_HEAD_INIT(mbr_list);
	int ret = 0;

	image_info(image, "writing protective MBR\n");

	memset(&mbr, 0, sizeof(struct partition));
	mbr.offset = 512;
	mbr.size = image->size - 512;
	mbr.in_partition_table = 1;
	mbr.partition_type = 0xee;
	list_add_tail(&mbr.list, &mbr_list);
	ret = hdimage_insert_mbr(image, &mbr_list);
	if (ret) {
		image_error(image,"failed to write protective MBR\n");
		return ret;
	}

	return 0;
}

static int hdimage_insert_gpt(struct image *image, struct list_head *partitions)
{
	struct hdimage *hd = image->handler_priv;
	const char *outfile = imageoutfile(image);
	struct gpt_header header;
	struct gpt_partition_entry table[GPT_ENTRIES];
	unsigned long long smallest_offset = ~0ULL;
	struct partition *part;
	unsigned i, j;
	int ret;

	image_info(image, "writing GPT\n");

	memset(&header, 0, sizeof(struct gpt_header));
	memcpy(header.signature, "EFI PART", 8);
	header.revision = htole32(GPT_REVISION_1_0);
	header.header_size = htole32(sizeof(struct gpt_header));
	header.current_lba = htole64(1);
	header.backup_lba = htole64(hd->gpt_no_backup ? 1 :image->size/512 - 1);
	header.first_usable_lba = htole64(~0ULL);
	header.last_usable_lba = htole64(image->size/512 - 1 - GPT_SECTORS);
	uuid_parse(hd->disk_uuid, header.disk_uuid);
	header.starting_lba = htole64(hd->gpt_location/512);
	header.number_entries = htole32(GPT_ENTRIES);
	header.entry_size = htole32(sizeof(struct gpt_partition_entry));

	i = 0;
	memset(&table, 0, sizeof(table));
	list_for_each_entry(part, partitions, list) {
		if (!part->in_partition_table)
			continue;

		if (part->offset < smallest_offset)
			smallest_offset = part->offset;

		uuid_parse(part->partition_type_uuid, table[i].type_uuid);
		uuid_parse(part->partition_uuid, table[i].uuid);
		table[i].first_lba = htole64(part->offset/512);
		table[i].last_lba = htole64((part->offset + part->size)/512 - 1);
		table[i].flags =
			(part->bootable ? GPT_PE_FLAG_BOOTABLE : 0) |
			(part->read_only ? GPT_PE_FLAG_READ_ONLY : 0) |
			(part->hidden ? GPT_PE_FLAG_HIDDEN : 0) |
			(part->no_automount ? GPT_PE_FLAG_NO_AUTO : 0);
		for (j = 0; j < strlen(part->name) && j < 36; j++)
			table[i].name[j] = htole16(part->name[j]);

		i++;
	}
	if (smallest_offset == ~0ULL)
		smallest_offset = hd->gpt_location + (GPT_SECTORS - 1)*512;
	header.first_usable_lba = htole64(smallest_offset / 512);


	header.table_crc = htole32(crc32(table, sizeof(table)));

	header.header_crc = htole32(crc32(&header, sizeof(header)));
	ret = insert_data(image, &header, outfile, sizeof(header), 512);
	if (ret) {
		image_error(image, "failed to write GPT\n");
		return ret;
	}
	ret = insert_data(image, &table, outfile, sizeof(table), hd->gpt_location);
	if (ret) {
		image_error(image, "failed to write GPT table\n");
		return ret;
	}

	if (!hd->gpt_no_backup) {
		ret = extend_file(image, image->size);
		if (ret) {
			image_error(image, "failed to pad image to size %lld\n",
				    image->size);
			return ret;
		}

		header.header_crc = 0;
		header.current_lba = htole64(image->size/512 - 1);
		header.backup_lba = htole64(1);
		header.starting_lba = htole64(image->size/512 - GPT_SECTORS);
		header.header_crc = htole32(crc32(&header, sizeof(header)));
		ret = insert_data(image, &table, outfile, sizeof(table),
				  image->size - GPT_SECTORS*512);
		if (ret) {
			image_error(image, "failed to write backup GPT table\n");
			return ret;
		}
		ret = insert_data(image, &header, outfile, sizeof(header),
				  image->size - 512);
		if (ret) {
			image_error(image, "failed to write backup GPT\n");
			return ret;
		}
	}

	if (hd->table_type == TYPE_HYBRID) {
		ret = hdimage_insert_mbr(image, partitions);
	} else {
		ret = hdimage_insert_protective_mbr(image);
	}
	if (ret) {
		return ret;
	}

	return 0;
}


static int hdimage_generate(struct image *image)
{
	struct partition *part;
	struct hdimage *hd = image->handler_priv;
	struct stat s;
	int ret;

	ret = prepare_image(image, hd->file_size);
	if (ret < 0)
		return ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;

		image_info(image, "adding %s partition '%s'%s%s%s%s ...\n",
			part->logical ? "logical" : "primary",
			part->name,
			part->in_partition_table ? " (in MBR)" : "",
			part->image ? " from '": "",
			part->image ? part->image : "",
			part->image ? "'" : "");

		if (part->logical) {
			ret = hdimage_insert_ebr(image, part);
			if (ret) {
				image_error(image, "failed to write EBR\n");
				return ret;
			}
		}

		if (!part->image)
			continue;

		child = image_get(part->image);

		if (child->size == 0 && !part->fill)
			continue;

		if (child->size > part->size) {
			image_error(image, "part %s size (%lld) too small for %s (%lld)\n",
				    part->name, part->size, child->file, child->size);
			return -E2BIG;
		}

		ret = insert_image(image, child, part->fill ? part->size : child->size, part->offset, 0);
		if (ret) {
			image_error(image, "failed to write image partition '%s'\n",
					part->name);
			return ret;
		}
	}

	if (hd->table_type != TYPE_NONE) {
		if (hd->table_type & TYPE_GPT) {
			ret = hdimage_insert_gpt(image, &image->partitions);
			if (ret)
				return ret;
		}
		else {
			ret = hdimage_insert_mbr(image, &image->partitions);
			if (ret)
				return ret;
		}
	}

	if (hd->fill) {
		ret = extend_file(image, image->size);
		if (ret) {
			image_error(image, "failed to fill the image.\n");
			return ret;
		}
	}

	if (!is_block_device(imageoutfile(image))) {
		ret = stat(imageoutfile(image), &s);
		if (ret) {
			ret = -errno;
			image_error(image, "stat(%s) failed: %s\n", imageoutfile(image),
				    strerror(errno));
			return ret;
		}
		if (hd->file_size != (unsigned long long)s.st_size) {
			image_error(image, "unexpected output file size: %llu != %llu\n",
				    hd->file_size, (unsigned long long)s.st_size);
			return -EINVAL;
		}
	}

	if (hd->table_type != TYPE_NONE)
		return reload_partitions(image);

	return 0;
}

static unsigned long long roundup(unsigned long long value, unsigned long long align)
{
	return ((value - 1)/align + 1) * align;
}

static unsigned long long rounddown(unsigned long long value, unsigned long long align)
{
	return value - (value % align);
}

static unsigned long long min_ull(unsigned long long x, unsigned long long y)
{
	return x < y ? x : y;
}

static unsigned long long max_ull(unsigned long long x, unsigned long long y)
{
	return x > y ? x : y;
}

static bool image_has_hole_covering(const char *image,
				    unsigned long long start, unsigned long long end)
{
	struct image *child;
	int i;

	if (!image)
		return false;
	child = image_get(image);
	for (i = 0; i < child->n_holes; ++i) {
		const struct extent *e = &child->holes[i];
		if (e->start <= start && end <= e->end)
			return true;
	}
	return false;
}

static int check_overlap(struct image *image, struct partition *p)
{
	unsigned long long start, end;
	struct partition *q;

	list_for_each_entry(q, &image->partitions, list) {
		/* Stop iterating when we reach p. */
		if (p == q)
			return 0;
		/* We must have that p starts beyond where q ends... */
		if (p->offset >= q->offset + q->size)
			continue;
		/* ...or vice versa. */
		if (q->offset >= p->offset + p->size)
			continue;

		/*
		 * Or maybe the image occupying the q partition has an
		 * area which it is ok to overwrite. We do not do the
		 * "vice versa" check, since images are written to the
		 * output file in the order the partitions are
		 * specified.
		 */
		start = max_ull(p->offset, q->offset);
		end = min_ull(p->offset + p->size, q->offset + q->size);

		if (image_has_hole_covering(q->image, start - q->offset, end - q->offset))
			continue;

		image_error(image,
			    "partition %s (offset 0x%llx, size 0x%llx) overlaps previous "
			    "partition %s (offset 0x%llx, size 0x%llx)\n",
			    p->name, p->offset, p->size,
			    q->name, q->offset, q->size);
		if (!q->in_partition_table &&
		    (!strcmp(p->name, "[MBR]") || !strncmp(p->name, "[GPT", 4)))
			image_error(image, "bootloaders, etc. that overlap with the "
				    "partition table must declare the overlapping "
				    "area as a hole.\n");
		return -EINVAL;
	}
	/* This should not be reached. */
	image_error(image, "linked list corruption???\n");
	return -EIO;
}

static struct partition *
fake_partition(const char *name, unsigned long long offset, unsigned long long size)
{
	struct partition *p = xzalloc(sizeof(*p));

	p->name = name;
	p->offset = offset;
	p->size = size;
	p->align = 1;
	return p;
}

static void ensure_extended_partition_index(struct image *image)
{
	struct hdimage *hd = image->handler_priv;
	struct partition *part;
	int count = 0;

	if (hd->extended_partition_index)
		return;

	list_for_each_entry(part, &image->partitions, list) {
		if (!part->in_partition_table)
			continue;

		if (++count > 4) {
			hd->extended_partition_index = 4;
			return;
		}
	}
}

static int setup_logical_partitions(struct image *image)
{
	struct hdimage *hd = image->handler_priv;
	struct partition *part;
	bool in_extended = false, found_extended = false;
	unsigned int count = 0, mbr_entries = 0;

	if (hd->extended_partition_index > 4) {
		image_error(image, "invalid extended partition index (%i). must be "
				"less or equal to 4 (0 for automatic)\n",
				hd->extended_partition_index);
		return -EINVAL;
	}

	if (hd->table_type != TYPE_MBR)
		return 0;

	ensure_extended_partition_index(image);

	if (!hd->extended_partition_index)
		return 0;

	list_for_each_entry(part, &image->partitions, list) {
		if (!part->in_partition_table)
			continue;

		++count;

		if (hd->extended_partition_index == count) {
			size_t offset = part->offset ? part->offset - hd->align : 0;
			struct partition *p = fake_partition("[Extended]", offset, 0);
			p->in_partition_table = true;
			p->partition_type = PARTITION_TYPE_EXTENDED;
			p->align = hd->align;

			hd->extended_partition = p;
			/* insert before the first logical partition */
			list_add_tail(&p->list, &part->list);

			in_extended = found_extended = true;
			++mbr_entries;
		}

		if (part->forced_primary)
			in_extended = false;
		if (in_extended && !part->forced_primary)
			part->logical = true;
		else
			++mbr_entries;

		if (part->forced_primary) {
			if (!found_extended) {
				image_error(image, "partition %s: forced-primary can only be used for "
						   "partitions following the extended partition\n",
					    part->name);
				return -EINVAL;
			}
		} else if (!in_extended && found_extended) {
			image_error(image,
				    "cannot create non-primary partition %s after forced-primary partition\n",
				    part->name);
			return -EINVAL;
		}
		if (mbr_entries > 4) {
			image_error(image, "too many primary partitions\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int setup_uuid(struct image *image, cfg_t *cfg)
{
	struct hdimage *hd = image->handler_priv;
	const char *disk_signature = cfg_getstr(cfg, "disk-signature");

	hd->disk_uuid = cfg_getstr(cfg, "disk-uuid");
	if (hd->disk_uuid) {
		if (!(hd->table_type & TYPE_GPT)) {
			image_error(image, "'disk-uuid' is only valid for gpt and hybrid partition-table-type\n");
			return -EINVAL;
		}
		if (uuid_validate(hd->disk_uuid) == -1) {
			image_error(image, "invalid disk UUID: %s\n", hd->disk_uuid);
			return -EINVAL;
		}
	}
	else {
		hd->disk_uuid = uuid_random();
	}

	if (!disk_signature)
		hd->disksig = 0;
	else if (!strcmp(disk_signature, "random"))
		hd->disksig = random();
	else {
		if (!(hd->table_type & TYPE_MBR)) {
			image_error(image, "'disk-signature' is only valid for mbr and hybrid partition-table-type\n");
			return -EINVAL;
		}
		hd->disksig = strtoul(disk_signature, NULL, 0);
	}
	return 0;
}

static int setup_part_autoresize(struct image *image, struct partition *part, bool *resized)
{
	struct hdimage *hd = image->handler_priv;
	long long partsize;

	if (!part->autoresize)
		return 0;

	if (*resized) {
		image_error(image, "'autoresize' is only supported "
			    "for one partition\n");
		return -EINVAL;
	}
	if (image->size == 0) {
		image_error(image, "the image size must be specified "
			    "when using an 'autoresize' partition\n");
		return -EINVAL;
	}

	partsize = image->size - part->offset;
	if (hd->table_type & TYPE_GPT)
		partsize -= GPT_SECTORS * 512;
	partsize = rounddown(partsize, part->align);
	if (partsize <= 0) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}
	if (partsize < (long long)part->size) {
		image_error(image, "auto-resize partition %s ends up with a size %lld"
			    " smaller than minimum %lld\n", part->name, partsize, part->size);
		return -EINVAL;
	}
	part->size = partsize;

	*resized = true;
	return 0;
}

static int setup_part_image(struct image *image, struct partition *part)
{
	struct hdimage *hd = image->handler_priv;
	struct image *child;

	if (!part->image)
		return 0;

	child = image_get(part->image);
	if (!child) {
		image_error(image, "could not find %s\n",
				part->image);
		return -EINVAL;
	}
	if (!part->size) {
		if (part->in_partition_table)
			part->size = roundup(child->size, part->align);
		else
			part->size = child->size;
	}
	if (child->size > part->size) {
		image_error(image, "part %s size (%lld) too small for %s (%lld)\n",
				part->name, part->size, child->file, child->size);
		return -EINVAL;
	}
	if (part->offset + child->size > hd->file_size) {
		size_t file_size = part->offset + child->size;
		if (file_size > hd->file_size)
			hd->file_size = file_size;
	}

	return 0;
}

static int hdimage_setup(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	unsigned int partition_table_entries = 0, hybrid_entries = 0;
	unsigned long long now = 0;
	const char *table_type;
	struct hdimage *hd = xzalloc(sizeof(*hd));
	struct partition *gpt_backup = NULL;
	bool partition_resized = false;
	int ret;

	image->handler_priv = hd;
	hd->align = cfg_getint_suffix(cfg, "align");
	hd->extended_partition_index = cfg_getint(cfg, "extended-partition");
	table_type = cfg_getstr(cfg, "partition-table-type");
	hd->gpt_location = cfg_getint_suffix(cfg, "gpt-location");
	hd->gpt_no_backup = cfg_getbool(cfg, "gpt-no-backup");
	hd->fill = cfg_getbool(cfg, "fill");

	if (is_block_device(imageoutfile(image))) {
		if (image->size) {
			image_error(image, "image size must not be specified for a block device target\n");
			return -EINVAL;
		}
		ret = block_device_size(image, imageoutfile(image), &image->size);
		if (ret)
			return ret;
		image_info(image, "determined size of block device %s to be %llu\n",
			   imageoutfile(image), image->size);
	}

	if (!strcmp(table_type, "none"))
		hd->table_type = TYPE_NONE;
	else if (!strcmp(table_type, "mbr") || !strcmp(table_type, "dos"))
		hd->table_type = TYPE_MBR;
	else if (!strcmp(table_type, "gpt"))
		hd->table_type = TYPE_GPT;
	else if (!strcmp(table_type, "hybrid"))
		hd->table_type = TYPE_HYBRID;
	else {
		image_error(image, "'%s' is not a valid partition-table-type\n",
				table_type);
		return -EINVAL;
	}
	if (cfg_size(cfg, "partition-table") > 0) {
		hd->table_type = cfg_getbool(cfg, "partition-table") ? TYPE_MBR : TYPE_NONE;
		image_info(image, "The option 'partition-table' is deprecated. Use 'partition-table-type' instead\n");
	}
	if (cfg_size(cfg, "gpt") > 0) {
		hd->table_type = cfg_getbool(cfg, "gpt") ? TYPE_GPT : TYPE_MBR;
		image_info(image, "The option 'gpt' is deprecated. Use 'partition-table-type' instead\n");
	}

	if (!hd->align)
		hd->align = hd->table_type == TYPE_NONE ? 1 : 512;

	if ((hd->table_type != TYPE_NONE) && ((hd->align % 512) || (hd->align == 0))) {
		image_error(image, "partition alignment (%lld) must be a "
				"multiple of 1 sector (512 bytes)\n", hd->align);
		return -EINVAL;
	}

	ret = setup_logical_partitions(image);
	if (ret < 0)
		return ret;

	ret = setup_uuid(image, cfg);
	if (ret < 0)
		return ret;

	if (hd->gpt_location == 0) {
		hd->gpt_location = 2*512;
	}
	else if (hd->gpt_location % 512) {
		image_error(image, "GPT table location (%lld) must be a "
				   "multiple of 1 sector (512 bytes)\n", hd->gpt_location);
	}

	if (hd->table_type != TYPE_NONE) {
		struct partition *mbr = fake_partition("[MBR]", 512 - sizeof(struct mbr_tail),
						       sizeof(struct mbr_tail));

		list_add_tail(&mbr->list, &image->partitions);
		now = partition_end(mbr);
		if (hd->table_type & TYPE_GPT) {
			struct partition *gpt_header, *gpt_array;
			unsigned long long backup_offset, backup_size;

			gpt_header = fake_partition("[GPT header]", 512, 512);
			gpt_array = fake_partition("[GPT array]", hd->gpt_location, (GPT_SECTORS - 1) * 512);
			list_add_tail(&gpt_header->list, &image->partitions);
			list_add_tail(&gpt_array->list, &image->partitions);
			now = partition_end(gpt_array);

			/* Includes both the backup header and array. */
			backup_size = GPT_SECTORS * 512;
			backup_offset = image->size ? image->size - backup_size : 0;
			gpt_backup = fake_partition("[GPT backup]", backup_offset, backup_size);
			list_add_tail(&gpt_backup->list, &image->partitions);
		}
	}

	partition_table_entries = 0;
	list_for_each_entry(part, &image->partitions, list) {
		if (hd->table_type == TYPE_NONE)
			part->in_partition_table = false;

		if (!part->align)
			part->align = (part->in_partition_table || hd->table_type == TYPE_NONE) ? hd->align : 1;
		if (part->in_partition_table && part->align % hd->align) {
			image_error(image, "partition alignment (%lld) of partition %s "
				    "must be multiple of image alignment (%lld)",
				    part->align, part->name, hd->align);
		}

		if (part->partition_type_uuid && !(hd->table_type & TYPE_GPT)) {
			image_error(image, "part %s: 'partition-type-uuid' is only valid for gpt and hybrid partition-table-type\n",
					part->name);
			return -EINVAL;
		}
		if (part->partition_type && !(hd->table_type & TYPE_MBR)) {
			image_error(image, "part %s: 'partition-type' is only valid for mbr and hybrid partition-table-type\n",
					part->name);
			return -EINVAL;
		}

		if ((hd->table_type & TYPE_GPT) && part->in_partition_table) {
			if (!part->partition_type_uuid)
				part->partition_type_uuid = "L";
			if (strlen(part->partition_type_uuid) > 0 &&
					uuid_validate(part->partition_type_uuid) != 0) {
				const char *uuid;
				uuid = gpt_partition_type_lookup(part->partition_type_uuid);
				if (!uuid) {
					image_error(image,
						    "part %s has invalid type shortcut: %s\n",
						    part->name, part->partition_type_uuid);
					return -EINVAL;
				}
				part->partition_type_uuid = uuid;
			}
			if (uuid_validate(part->partition_type_uuid) == -1) {
				image_error(image,
					    "part %s has invalid partition type UUID: %s\n",
					    part->name, part->partition_type_uuid);
				return -EINVAL;
			}
			if (part->partition_uuid) {
				if (uuid_validate(part->partition_uuid) == -1) {
					image_error(image,
						    "part %s has invalid partition UUID: %s\n",
						    part->name, part->partition_uuid);
					return -EINVAL;
				}
			}
			else {
				part->partition_uuid = uuid_random();
			}
			if (part->partition_type)
				++hybrid_entries;
		}
		if (part->in_partition_table)
			++partition_table_entries;
		if (part->logical) {
			/* reserve space for extended boot record */
			now += hd->align;
			now = roundup(now, part->align);
		}
		if (part == gpt_backup && !part->offset) {
			/*
			 * Make sure the backup, and hence the whole
			 * image, ends at a 4K boundary.
			 */
			now += part->size;
			part->offset = roundup(now, 4096) - part->size;
		}
		if (!part->offset && (part->in_partition_table || hd->table_type == TYPE_NONE)) {
			part->offset = roundup(now, part->align);
		}

		if (part->offset % part->align) {
			image_error(image, "part %s offset (%lld) must be a"
					"multiple of %lld bytes\n",
					part->name, part->offset, part->align);
			return -EINVAL;
		}
		ret = setup_part_autoresize(image, part, &partition_resized);
		if (ret < 0)
			return ret;

		ret = setup_part_image(image, part);
		if (ret < 0)
			return ret;

		/* the size of the extended partition will be filled in later */
		if (!part->size && part != hd->extended_partition) {
			image_error(image, "part %s size must not be zero\n",
					part->name);
			return -EINVAL;
		}
		if (!part->logical) {
			ret = check_overlap(image, part);
			if (ret)
				return ret;
		} else if (now > part->offset) {
			image_error(image, "part %s overlaps with previous partition\n",
					part->name);
			return -EINVAL;
		}
		if (part->in_partition_table && (part->size % 512)) {
			image_error(image, "part %s size (%lld) must be a "
					"multiple of 1 sector (512 bytes)\n",
					part->name, part->size);
			return -EINVAL;
		}
		if (part->offset + part->size > now)
			now = part->offset + part->size;

		if (part->logical) {
			size_t file_size = part->offset - hd->align + 512;
			if (file_size > hd->file_size)
				hd->file_size = file_size;
		}

		if (part->logical) {
			hd->extended_partition->size = now - hd->extended_partition->offset;
		}
	}

	if (hybrid_entries > 3) {
		image_error(image, "hybrid MBR partitions (%i) exceeds maximum of 3\n",
			    hybrid_entries);
		return -EINVAL;
	}
	if (hd->table_type == TYPE_HYBRID && hybrid_entries == 0) {
		image_error(image, "no partition with partition-type but hybrid partition-table-type selected\n");
		return -EINVAL;
	}

	if (image->size > 0 && now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}

	if (image->size == 0) {
		image->size = now;
	}

	if (hd->fill || ((hd->table_type & TYPE_GPT) && !hd->gpt_no_backup))
		hd->file_size = image->size;

	return 0;
}

static cfg_opt_t hdimage_opts[] = {
	CFG_STR("align", NULL, CFGF_NONE),
	CFG_STR("disk-signature", NULL, CFGF_NONE),
	CFG_STR("disk-uuid", NULL, CFGF_NONE),
	CFG_BOOL("partition-table", cfg_false, CFGF_NODEFAULT),
	CFG_INT("extended-partition", 0, CFGF_NONE),
	CFG_STR("partition-table-type", "mbr", CFGF_NONE),
	CFG_BOOL("gpt", cfg_false, CFGF_NODEFAULT),
	CFG_STR("gpt-location", NULL, CFGF_NONE),
	CFG_BOOL("gpt-no-backup", cfg_false, CFGF_NONE),
	CFG_BOOL("fill", cfg_false, CFGF_NONE),
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.no_rootpath = cfg_true,
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

