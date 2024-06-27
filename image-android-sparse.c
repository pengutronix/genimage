/*
 * Copyright (c) 2021 Michael Olbrich <m.olbrich@pengutronix.de>
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
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "genimage.h"

struct sparse {
	uint32_t block_size;
};

struct sparse_header {
	uint32_t magic;
	uint16_t major_version;
	uint16_t minor_version;
	uint16_t header_size;
	uint16_t chunk_header_size;
	uint32_t block_size;
	uint32_t output_blocks;
	uint32_t input_chunks;
	uint32_t crc32;
} __attribute__((packed));

#define SPARSE_RAW		htole16(0xCAC1)
#define SPARSE_FILL		htole16(0xCAC2)
#define SPARSE_DONT_CARE	htole16(0xCAC3)
#define SPARSE_CRC32		htole16(0xCAC4)

struct sparse_chunk_header {
	uint16_t chunk_type;
	uint16_t reserved;
	uint32_t blocks;
	uint32_t size;
} __attribute__((packed));

static int write_data(struct image *image, int fd, const void *data, size_t size)
{
	int ret = 0;

	ssize_t written = write(fd, data, size);
	if (written < 0) {
		ret = -errno;
		image_error(image, "write %s: %s\n", imageoutfile(image), strerror(errno));
	}
	else if ((size_t)written != size) {
		image_error(image, "only %llu bytes written instead of %llu\n",
			    (unsigned long long)written, (unsigned long long)size);
		ret = -EINVAL;
	}
	return ret;
}

static int flush_header(struct image *image, int fd, struct sparse_chunk_header *header,
			ssize_t offset)
{
	int ret;

	if (header->chunk_type == 0)
		return 0;

	if (offset >= 0) {
		if (lseek(fd, offset, SEEK_SET) < 0) {
			ret = -errno;
			image_error(image, "seek %s: %s\n", imageoutfile(image),
				    strerror(errno));
			return ret;
		}
	}

	ret = write_data(image, fd, header, sizeof(*header));
	if (ret < 0)
		return ret;

	if (offset >= 0) {
		if (lseek(fd, 0, SEEK_END) < 0) {
			ret = -errno;
			image_error(image, "seek %s: %s\n", imageoutfile(image),
				    strerror(errno));
			return ret;
		}
	}
	if (header->blocks > 0 || header->chunk_type == SPARSE_CRC32)
		image_debug(image, "chunk(0x%04x): blocks =%7u size =%10u bytes\n",
			    header->chunk_type, header->blocks, header->size);

	return 0;
}

static int android_sparse_generate(struct image *image)
{
	struct sparse *sparse = image->handler_priv;
	struct image *inimage;
	const char *infile;
	struct sparse_header header;
	struct sparse_chunk_header chunk_header = {};
	struct extent *extents = NULL;
	size_t extent_count, extent, block_count, block;
	int in_fd = -1, out_fd = -1, ret;
	off_t offset;
	unsigned int i;
	uint32_t *buf, *zeros, crc32 = 0;
	struct stat s;

	memset(&header, 0, sizeof(header));
	header.magic = htole32(0xed26ff3a);
	header.major_version = htole16(0x1);
	header.minor_version = htole16(0x0);
	header.header_size = htole16(sizeof(struct sparse_header));
	header.chunk_header_size = htole16(sizeof(struct sparse_chunk_header));
	header.block_size = sparse->block_size;

	inimage = image_get(list_first_entry(&image->partitions, struct partition, list)->image);
	infile = imageoutfile(inimage);

	in_fd = open(infile, O_RDONLY);
	if (in_fd < 0) {
		ret = -errno;
		image_error(image, "open %s: %s\n", infile, strerror(errno));
		return ret;
	}
	ret = fstat(in_fd, &s);
	if (ret) {
		ret = -errno;
		image_error(image, "stat %s: %s\n", infile, strerror(errno));
		goto out;
	}
	block_count = (s.st_size - 1 + sparse->block_size) / sparse->block_size;
	header.output_blocks = block_count;

	ret = map_file_extents(inimage, infile, in_fd, s.st_size, &extents, &extent_count);
	if (ret < 0)
		goto out;

	/* The extents may have a different granularity than the chosen block size.
	   So all start and end of all extents must be aligned accordingly. The
	   extents may overlap now, so merge them if necessary. */
	for (extent = 0; extent < extent_count; ++extent) {
		size_t size, max;
		int j;

		extents[extent].start = extents[extent].start / sparse->block_size *
					sparse->block_size;
		extents[extent].end = (extents[extent].end - 1 + sparse->block_size) /
				      sparse->block_size * sparse->block_size;
		extents[extent].end = min(extents[extent].end, s.st_size);

		for (j = extent - 1; j > 0; --j)
			if (extents[j].end != 0)
				break;

		if (j >= 0 && extents[extent].start <= extents[j].end) {
			extents[j].end = extents[extent].end;
			extents[extent].start = 0;
			extents[extent].end = 0;
		}

		/* TODO: split extents that are too big */
		max = (~(uint32_t)0) - sizeof(struct sparse_chunk_header);
		size = extents[extent].end - extents[extent].start;
		if (size > max) {
			image_error(image, "extents size %llu larger and supported maximum %llu.\n",
					(unsigned long long)size, (unsigned long long)max);
			ret = -EINVAL;
			goto out;
		}
	}

	out_fd = open_file(image, imageoutfile(image), O_TRUNC);
	if (out_fd < 0) {
		ret = out_fd;
		goto out;
	}

	ret = write_data(image, out_fd, &header, sizeof(header));
	if (ret < 0)
		goto out;

	block = 0;
	buf = xzalloc(sparse->block_size);
	zeros = xzalloc(sparse->block_size);
	memset(zeros, 0, sparse->block_size);
	for (extent = 0; extent < extent_count; ++extent) {
		uint32_t start_block = extents[extent].start / sparse->block_size;
		size_t size = extents[extent].end - extents[extent].start;
		uint32_t fill_value = 0;
		size_t pos;

		/* skip removed extents */
		if (size == 0)
			continue;

		if (block < start_block) {
			header.input_chunks++;
			chunk_header.chunk_type = SPARSE_DONT_CARE;
			chunk_header.blocks = start_block - block;
			chunk_header.size = sizeof(chunk_header);
			ret = flush_header(image, out_fd, &chunk_header, -1);
			if (ret < 0)
				return ret;
			block = start_block;

			for (i = 0; i < chunk_header.blocks; ++i)
				crc32 = crc32_next(zeros, sparse->block_size, crc32);
		}
		offset = lseek(in_fd, extents[extent].start, SEEK_SET);
		if (offset < 0) {
			ret = -errno;
			image_error(image, "seek %s: %s\n", infile, strerror(errno));
			goto out;
		}
		chunk_header.chunk_type = 0;
		chunk_header.blocks = 0;
		pos = lseek(out_fd, 0, SEEK_CUR);

		while (size > 0) {
			ssize_t now = min(size, sparse->block_size);
			int fill = 1;
			ssize_t r = read(in_fd, buf, now);

			if (r < 0) {
				ret = -errno;
				image_error(image, "read %s: %s\n", infile, strerror(errno));
				goto out;
			}
			else if (r != now) {
				ret = -EINVAL;
				image_error(image, "short read %s %lld != %lld\n", infile,
					    (long long)r, (long long)now);
				goto out;
			}

			/* The sparse format only allows image sizes that are a multiple of
			   the block size. Pad the last block as needed. */
			if ((uint32_t)r < sparse->block_size) {
				memset(((char*)buf)+ r, 0, sparse->block_size - r);
				now = sparse->block_size;
			}

			crc32 = crc32_next(buf, sparse->block_size, crc32);

			for (i = 1; i < sparse->block_size/4; ++i) {
				if (buf[0] != buf[i]) {
					fill = 0;
					break;
				}
			}
			if (fill) {
				if (chunk_header.chunk_type != SPARSE_FILL ||
				    fill_value != buf[0]) {
					header.input_chunks++;
					ret = flush_header(image, out_fd, &chunk_header, pos);
					if (ret < 0)
						return ret;

					pos = lseek(out_fd, 0, SEEK_CUR);
					chunk_header.chunk_type = SPARSE_FILL;
					chunk_header.size = sizeof(chunk_header) + sizeof(buf[0]);
					chunk_header.blocks = 0;
					fill_value = buf[0];

					ret = flush_header(image, out_fd, &chunk_header, -1);
					if (ret < 0)
						return ret;
					ret = write_data(image, out_fd, buf, sizeof(buf[0]));
					if (ret < 0)
						goto out;
				}
				chunk_header.blocks++;
			}
			else {
				if (chunk_header.chunk_type != SPARSE_RAW) {
					header.input_chunks++;
					ret = flush_header(image, out_fd, &chunk_header, pos);
					if (ret < 0)
						return ret;

					pos = lseek(out_fd, 0, SEEK_CUR);
					chunk_header.chunk_type = SPARSE_RAW;
					chunk_header.size = sizeof(chunk_header);
					chunk_header.blocks = 0;
					ret = flush_header(image, out_fd, &chunk_header, -1);
					if (ret < 0)
						return ret;
				}
				chunk_header.blocks++;
				chunk_header.size += now;

				ret = write_data(image, out_fd, buf, now);
				if (ret < 0)
					goto out;
			}

			size -= r;
		}
		ret = flush_header(image, out_fd, &chunk_header, pos);
		if (ret < 0)
			return ret;
		block = (extents[extent].end - 1 + sparse->block_size) / sparse->block_size;
	}

	if (block < block_count) {
		header.input_chunks++;
		chunk_header.chunk_type = SPARSE_DONT_CARE;
		chunk_header.blocks = block_count - block;
		chunk_header.size = sizeof(chunk_header);
		ret = flush_header(image, out_fd, &chunk_header, -1);
		if (ret < 0)
			return ret;

		for (i = 0; i < chunk_header.blocks; ++i)
			crc32 = crc32_next(zeros, sparse->block_size, crc32);
	}

	header.input_chunks++;
	chunk_header.chunk_type = SPARSE_CRC32;
	chunk_header.blocks = 0;
	chunk_header.size = sizeof(chunk_header) + sizeof(crc32);
	ret = flush_header(image, out_fd, &chunk_header, -1);
	if (ret < 0)
		return ret;
	ret = write_data(image, out_fd, &crc32, sizeof(crc32));
	if (ret < 0)
		goto out;

	offset = lseek(out_fd, 0, SEEK_SET);
	if (offset < 0) {
		ret = -errno;
		image_error(image, "seek %s: %s\n", infile, strerror(errno));
		goto out;
	}
	ret = write_data(image, out_fd, &header, sizeof(header));
	if (ret < 0)
		goto out;

	image_info(image, "sparse image with %u chunks and %u blocks\n",
		   header.input_chunks, header.output_blocks);

out:
	close(in_fd);
	if (out_fd >= 0)
		close(out_fd);
	if (extents)
		free(extents);
	return ret;
}

static int android_sparse_parse(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	char *src;

	src = cfg_getstr(image->imagesec, "image");
	if (!src) {
		image_error(image, "Mandatory 'image' option is missing!\n");
		return -EINVAL;
	}
	image_info(image, "input image: %s\n", src);

	part = xzalloc(sizeof *part);
	part->image = src;
	list_add_tail(&part->list, &image->partitions);

	return 0;
}

static int android_sparse_setup(struct image *image, cfg_t *cfg)
{
	struct sparse *sparse = xzalloc(sizeof(*sparse));

	sparse->block_size = cfg_getint_suffix(cfg, "block-size");
	if (sparse->block_size % 512) {
		image_error(image, "block-size %u invalid. It must be a multiple of 512!\n",
			    sparse->block_size);
		return -EINVAL;
	}

	image->handler_priv = sparse;
	return 0;
}

static cfg_opt_t android_sparse_opts[] = {
	CFG_STR("image", NULL, CFGF_NONE),
	CFG_STR("block-size", "4k", CFGF_NONE),
	CFG_END()
};

struct image_handler android_sparse_handler = {
	.type = "android-sparse",
	.no_rootpath = cfg_true,
	.generate = android_sparse_generate,
	.parse = android_sparse_parse,
	.setup = android_sparse_setup,
	.opts = android_sparse_opts,
};
