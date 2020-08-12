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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>

#include "genimage.h"

#ifndef AT_NO_AUTOMOUNT
#define AT_NO_AUTOMOUNT 0x800
#endif

static int loglevel(void)
{
	static int level = -1;

	if (level < 0) {
		const char *l = get_opt("loglevel");
		if (l)
			level = atoi(l);
		else
			level = 1;
	}

	return level;
}

static int skip_log(int level)
{
	return (level > loglevel());
}

static void xvasprintf(char **strp, const char *fmt, va_list ap)
{
	if (vasprintf(strp, fmt, ap) < 0) {
		error("out of memory\n");
		exit(1);
	}
}

void xasprintf(char **strp, const char *fmt, ...)
{
	va_list args;

	va_start (args, fmt);

	xvasprintf(strp, fmt, args);

	va_end (args);
}

static void image_log(struct image *image, int level, const char *fmt,
		      va_list args)
{
	char *buf;
	const char *p;

	if (skip_log(level))
		return;

	xvasprintf(&buf, fmt, args);

	switch (level) {
		case 0:
			p = "ERROR";
			break;
		case 1:
			p = "INFO";
			break;
		case 2:
			p = "DEBUG";
			break;
		case 3:
		default:
			p = "VDEBUG";
			break;
	}

	if (image)
		fprintf(stderr, "%s: %s(%s): %s", p, image->handler ?
			image->handler->type : "unknown", image->file, buf);
	else
		fprintf(stderr, "%s: %s", p, buf);

	free(buf);
}

void image_error(struct image *image, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(image, 0, fmt, args);
	va_end(args);
}

void image_info(struct image *image, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(image, 1, fmt, args);
	va_end(args);
}

void image_debug(struct image *image, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(image, 2, fmt, args);
	va_end(args);
}

void error(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(NULL, 0, fmt, args);
	va_end(args);
}

void info(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(NULL, 1, fmt, args);
	va_end(args);
}

void debug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	image_log(NULL, 2, fmt, args);
	va_end(args);
}

/*
 * printf wrapper around 'system'
 */
int systemp(struct image *image, const char *fmt, ...)
{
	va_list args;
	char *buf;
	const char *o;
	int ret;
	int status;
	pid_t pid;

	va_start (args, fmt);

	xvasprintf(&buf, fmt, args);

	va_end (args);

	if (!buf)
		return -ENOMEM;

	if (loglevel() >= 3)
		o = " (stderr+stdout):";
	else if (loglevel() >= 1)
		o = " (stderr):";
	else
		o = "";

	image_info(image, "cmd: \"%s\"%s\n", buf, o);

	pid = fork();

	if (!pid) {
		int fd;

		if (loglevel() < 1) {
			fd = open("/dev/null", O_WRONLY);
			dup2(fd, STDERR_FILENO);
		}

		if (loglevel() < 3) {
			fd = open("/dev/null", O_WRONLY);
			dup2(fd, STDOUT_FILENO);
		} else {
			dup2(STDERR_FILENO, STDOUT_FILENO);
		}

		ret = execl("/bin/sh", "sh", "-c", buf, NULL);
		if (ret < 0) {
			ret = -errno;
			error("Cannot execute %s: %s\n", buf, strerror(errno));
			goto err_out;
		}
	} else {
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			ret = -errno;
			error("Failed to wait for command execution: %s\n", strerror(errno));
			goto err_out;
		}
	}

	ret = WEXITSTATUS(status);

err_out:
	free(buf);

	return ret;
}

/*
 * xzalloc - safely allocate zeroed memory
 */
void *xzalloc(size_t n)
{
	void *m = malloc(n);

	if (!m) {
		error("out of memory\n");
		exit(1);
	}

	memset(m, 0, n);

	return m;
}

/*
 * Like simple_strtoul() but handles an optional G, M, K or k
 * suffix for Gigabyte, Megabyte or Kilobyte
 */
unsigned long long strtoul_suffix(const char *str, char **endp,
		cfg_bool_t *percent)
{
	unsigned long long val;
	char *end;

	val = strtoull(str, &end, 0);

	if (percent)
		*percent = cfg_false;

	switch (*end) {
	case 'G':
		val *= 1024;
		/* fall-through */
	case 'M':
		val *= 1024;
		/* fall-through */
	case 'k':
	case 'K':
		val *= 1024;
		end++;
	case '\0':
		break;
	case '%':
		if (percent) {
			*percent = cfg_true;
			break;
		}
		/* fall-through */
	default:
		error("Invalid size suffix '%s' in '%s'\n", end, str);
		exit(1);
	}

	if (endp)
		*endp = (char *)end;

	return val;
}

static size_t min(size_t a, size_t b)
{
	return a < b ? a : b;
}

int is_block_device(const char *filename) {
	struct stat s;
	return stat(filename, &s) == 0 && ((s.st_mode & S_IFMT) == S_IFBLK);
}

static int open_file(struct image *image, const char *filename, int extra_flags)
{
	int flags = O_WRONLY | extra_flags;
	int ret, fd;

	/* make sure block devices are unused before writing */
	if (is_block_device(filename))
		flags |= O_EXCL;
	else
		flags |= O_CREAT;

	fd = open(filename, flags, 0666);
	if (fd < 0) {
		ret = -errno;
		image_error(image, "open %s: %s\n", filename, strerror(errno));
		return ret;
	}
	return fd;
}

struct extent {
	unsigned long long start, end;
};

/* Build a file extent covering the whole file */
static int whole_file_exent(size_t size, struct extent **extents,
			    size_t *extent_count)
{
	*extents = xzalloc(sizeof(struct extent));
	(*extents)[0].start = 0;
	(*extents)[0].end = size;
	*extent_count = 1;
	return 0;
}

/* Build an file extent array for the file */
static int map_file_extents(struct image *image, const char *filename, int f,
			    size_t size, struct extent **extents,
			    size_t *extent_count)
{
	struct fiemap *fiemap;
	unsigned i;
	int ret;

	/* Get extent count */
	fiemap = xzalloc(sizeof(struct fiemap));
	fiemap->fm_length = size;
	ret = ioctl(f, FS_IOC_FIEMAP, fiemap);
	if (ret == -1)
		goto err_out;

	/* Get extents */
	fiemap = realloc(fiemap, sizeof(struct fiemap) + fiemap->fm_mapped_extents * sizeof(struct fiemap_extent));
	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	ret = ioctl(f, FS_IOC_FIEMAP, fiemap);
	if (ret == -1)
		goto err_out;

	/* Build extent array */
	*extent_count = fiemap->fm_extent_count;
	*extents = xzalloc(*extent_count * sizeof(struct extent));
	for (i = 0; i < fiemap->fm_mapped_extents; i++) {
		(*extents)[i].start = fiemap->fm_extents[i].fe_logical;
		(*extents)[i].end = fiemap->fm_extents[i].fe_logical + fiemap->fm_extents[i].fe_length;
	}
	free(fiemap);

	/* The last extent may extend beyond the end of file, limit it to the actual end */
	if (fiemap->fm_mapped_extents && (*extents)[i-1].end > size)
		(*extents)[i-1].end = size;

	return 0;

err_out:
	ret = -errno;

	free(fiemap);

	/* If failure is due to no filesystem support, return a single extent */
	if (ret == -EOPNOTSUPP || ret == -ENOTTY)
		return whole_file_exent(size, extents, extent_count);

	image_error(image, "fiemap %s: %d %s\n", filename, errno, strerror(errno));
	return ret;
}

int pad_file(struct image *image, const char *infile,
		size_t size, unsigned char fillpattern, enum pad_mode mode)
{
	const char *outfile = imageoutfile(image);
	int f = -1, outf = -1, flags = 0;
	unsigned long f_offset = 0;
	struct extent *extents;
	size_t extent_count = 0;
	void *buf = NULL;
	int now, r, w;
	unsigned e;
	struct stat s;
	int ret = 0;

	if (infile) {
		f = open(infile, O_RDONLY);
		if (f < 0) {
			ret = -errno;
			image_error(image, "open %s: %s\n", infile, strerror(errno));
			goto err_out;
		}
	}
	if (mode == MODE_OVERWRITE) {
		image->last_offset = 0;
		flags = O_TRUNC;
	}
	outf = open_file(image, outfile, flags);
	if (outf < 0) {
		ret = outf;
		goto err_out;
	}

	ret = fstat(outf, &s);
	if (ret) {
		ret = -errno;
		image_error(image, "stat %s: %s\n", outfile, strerror(errno));
		goto err_out;
	}
	if (((s.st_mode & S_IFMT) == S_IFREG) && (mode == MODE_APPEND)) {
		off_t offset = lseek(outf, 0, SEEK_END);
		if (offset < 0) {
			ret = -errno;
			image_error(image, "seek: %s\n", strerror(errno));
			goto err_out;
		}
		if (offset != image->last_offset) {
			ret = -EINVAL;
			image_error(image, "pad_file: unexpected offset: %lld !=  %lld\n",
					(long long)offset, (long long)image->last_offset);
			goto err_out;
		}
	}
	if (((s.st_mode & S_IFMT) == S_IFBLK) && (mode == MODE_APPEND)) {
		if (lseek(outf, image->last_offset, SEEK_SET) < 0) {
			ret = -errno;
			image_error(image, "seek: %s\n", strerror(errno));
			goto err_out;
		}
	}
	if (((s.st_mode & S_IFMT) != S_IFREG) &&
			((s.st_mode & S_IFMT) != S_IFBLK)) {
		ret = -EINVAL;
		image_error(image, "pad_file: not a regular file or block device\n");
		goto err_out;
	}

	buf = xzalloc(4096);

	if ((unsigned long long)s.st_size > size) {
		image_error(image, "output file '%s' too large\n", outfile);
		ret = -EINVAL;
		goto err_out;
	}
	size = size - s.st_size;
	if (!infile)
		goto fill;

	if ((s.st_mode & S_IFMT) == S_IFREG) {
		ret = map_file_extents(image, infile, f, size, &extents, &extent_count);
		if (ret != 0)
			goto err_out;
	}
	else {
		whole_file_exent(size, &extents, &extent_count);
	}

	image_debug(image, "copying %zu bytes from %s at offset %zd\n",
			size, infile, image->last_offset);

	for (e = 0; e < extent_count && size > 0; e++) {
		image_debug(image, "copying [%lld,%lld]\n", extents[e].start, extents[e].end);
		/* Ship over any holes in the input file */
		if (f_offset != extents[e].start) {
			unsigned long skip = extents[e].start - f_offset;
			lseek(f, skip, SEEK_CUR);
			lseek(outf, skip, SEEK_CUR);
			size -= skip;
			f_offset += skip;
		}

		/* Copy the data in the extent */
		while (f_offset < extents[e].end) {
			now = min(extents[e].end - f_offset, 4096);

			r = read(f, buf, now);
			w = write(outf, buf, r);
			if (w < r) {
				ret = -errno;
				image_error(image, "write %s: %s\n", outfile, strerror(errno));
				goto err_out;
			}
			size -= r;
			f_offset += r;

			if (r < now)
				goto fill;
		}
	}

fill:
	if (fillpattern == 0 && (s.st_mode & S_IFMT) == S_IFREG) {
		/* Truncate output to desired size */
		image->last_offset = lseek(outf, 0, SEEK_CUR) + size;
		ret = ftruncate(outf, image->last_offset);
		if (ret == -1) {
			ret = -errno;
			image_error(image, "ftruncate %s: %s\n", outfile, strerror(errno));
			goto err_out;
		}
	}
	else {
		memset(buf, fillpattern, 4096);

		while (size) {
			now = min(size, 4096);

			r = write(outf, buf, now);
			if (r < now) {
				ret = -errno;
				image_error(image, "write %s: %s\n", outfile, strerror(errno));
				goto err_out;
			}
			size -= now;
		}
		image->last_offset = lseek(outf, 0, SEEK_CUR);
	}
err_out:
	free(buf);
	if (f >= 0)
		close(f);
	if (outf >= 0)
		close(outf);

	return ret;
}

int insert_data(struct image *image, const char *data, const char *outfile,
		size_t size, long offset)
{
	int outf = -1;
	int now, r;
	int ret = 0;

	outf = open_file(image, outfile, 0);
	if (outf < 0)
		return outf;

	if (lseek(outf, offset, SEEK_SET) < 0) {
		ret = -errno;
		image_error(image, "seek %s: %s\n", outfile, strerror(errno));
		goto err_out;
	}
	while (size) {
		now = min(size, 4096);

		r = write(outf, data, now);
		if (r < now) {
			ret = -errno;
			image_error(image, "write %s: %s\n", outfile, strerror(errno));
			goto err_out;
		}
		size -= now;
		data += now;
	}
err_out:
	close(outf);

	return ret;
}

int extend_file(struct image *image, size_t size)
{
	const char *outfile = imageoutfile(image);
	int f;
	off_t offset;
	int ret = 0;

	f = open_file(image, outfile, 0);
	if (f < 0)
		return f;

	offset = lseek(f, 0, SEEK_END);
	if (offset < 0) {
		ret = -errno;
		image_error(image, "seek: %s\n", strerror(errno));
		goto out;
	}
	if ((size_t)offset > size) {
		ret = -EINVAL;
		image_error(image, "output file is larger than requested size\n");
		goto out;
	}
	if ((size_t)offset == size)
		goto out;

	ret = ftruncate(f, size);
	if (ret == -1) {
		ret = -errno;
		image_error(image, "ftruncate %s: %s\n", outfile, strerror(errno));
		goto out;
	}
	ret = 0;
out:
	close(f);
	return ret;
}

int uuid_validate(const char *str)
{
	int i;

	if (strlen(str) != 36)
		return -1;
	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (str[i] != '-')
				return -1;
			continue;
		}
		if (!isxdigit(str[i]))
			return -1;
	}

	return 0;
}

static unsigned char uuid_byte(const char *hex)
{
	char buf[3];

	buf[0] = hex[0];
	buf[1] = hex[1];
	buf[2] = 0;
	return strtoul(buf, NULL, 16);
}

void uuid_parse(const char *str, unsigned char *uuid)
{
	uuid[0] = uuid_byte(str + 6);
	uuid[1] = uuid_byte(str + 4);
	uuid[2] = uuid_byte(str + 2);
	uuid[3] = uuid_byte(str);

	uuid[4] = uuid_byte(str + 11);
	uuid[5] = uuid_byte(str + 9);

	uuid[6] = uuid_byte(str + 16);
	uuid[7] = uuid_byte(str + 14);

	uuid[8] = uuid_byte(str + 19);
	uuid[9] = uuid_byte(str + 21);

	uuid[10] = uuid_byte(str + 24);
	uuid[11] = uuid_byte(str + 26);
	uuid[12] = uuid_byte(str + 28);
	uuid[13] = uuid_byte(str + 30);
	uuid[14] = uuid_byte(str + 32);
	uuid[15] = uuid_byte(str + 34);
}

char *uuid_random(void)
{
	char *uuid;

	asprintf(&uuid, "%04lx%04lx-%04lx-%04lx-%04lx-%04lx%04lx%04lx",
		 random() & 0xffff, random() & 0xffff,
		 random() & 0xffff,
		 (random() & 0x0fff) | 0x4000,
		 (random() & 0x3fff) | 0x8000,
		 random() & 0xffff, random() & 0xffff, random() & 0xffff);

	return uuid;
}

int reload_partitions(struct image *image)
{
	const char *outfile = imageoutfile(image);
	int fd;

	if (!is_block_device(outfile))
		return 0;

	fd = open(outfile, O_WRONLY|O_EXCL);
	if (fd < 0) {
		int ret = -errno;
		image_error(image, "open: %s\n", strerror(errno));
		return ret;
	}
	/* no error because not all block devices support this */
	if (ioctl(fd, BLKRRPART) < 0)
		image_info(image, "failed to re-read partition table: %s\n",
			strerror(errno));
	close(fd);
	return 0;
}

#define ROUND_UP(num,align) ((((num) + ((align) - 1)) & ~((align) - 1)))

static unsigned long long dir_size(struct image *image, int dirfd,
		const char *subdir, size_t blocksize)
{
	struct dirent *d;
	DIR *dir;
	int fd;
	unsigned long long size = 0;
	struct stat st;

	fd = openat(dirfd, subdir, O_RDONLY);
	if (fd < 0) {
		image_error(image, "failed to open '%s': %s", subdir,
				strerror(errno));
		return 0;
	}

	dir = fdopendir(dup(fd));
	if (dir == NULL) {
		image_error(image, "failed to opendir '%s': %s", subdir,
				strerror(errno));
		close(fd);
		return 0;
	}
	while ((d = readdir(dir)) != NULL) {
		if (d->d_type == DT_DIR) {
			if (d->d_name[0] == '.' && (d->d_name[1] == '\0' ||
			    (d->d_name[1] == '.' && d->d_name[2] == '\0')))
				continue;
			size += dir_size(image, fd, d->d_name, blocksize);
			continue;
		}
		if (d->d_type != DT_REG)
			continue;
		if (fstatat(fd,  d->d_name, &st, AT_NO_AUTOMOUNT) < 0) {
			image_error(image, "failed to stat '%s': %s",
					d->d_name, strerror(errno));
			continue;
		}
		size += ROUND_UP(st.st_size, blocksize);
	}
	closedir(dir);
	close(fd);
	return size + blocksize;
}

unsigned long long image_dir_size(struct image *image)
{
	if (image->empty)
		return 0;
	return dir_size(image, AT_FDCWD, mountpath(image), 4096);
}
