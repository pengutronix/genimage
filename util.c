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
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_FIEMAP
#include <linux/fiemap.h>
#endif
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

void xstrcatf(char **strp, const char *fmt, ...)
{
	char *tmp;
	va_list list;

	va_start(list, fmt);
	xvasprintf(&tmp, fmt, list);
	va_end(list);
	if (*strp) {
	        *strp = xrealloc(*strp, strlen(*strp) + strlen(tmp) + 1);
	        strcat(*strp, tmp);
	        free(tmp);
	} else {
	        *strp = tmp;
	}
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
		const char *shell;
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

		shell = getenv("GENIMAGE_SHELL");
		if (!shell || shell[0] == 0x0)
			shell = "/bin/sh";

		execl(shell, shell, "-c", buf, NULL);
		ret = -errno;
		error("Cannot execute %s: %s\n", buf, strerror(errno));
		goto err_out;
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

void *xrealloc(void *ptr, size_t size)
{
	void *m = realloc(ptr, size);

	if (!m) {
		error("out of memory\n");
		exit(1);
	}

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
		break;
	case 's':
		val *= 512;
		end++;
		break;
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

int is_block_device(const char *filename) {
	struct stat s;
	return stat(filename, &s) == 0 && ((s.st_mode & S_IFMT) == S_IFBLK);
}

int open_file(struct image *image, const char *filename, int extra_flags)
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
int map_file_extents(struct image *image, const char *filename, int f,
		     size_t size, struct extent **extents, size_t *extent_count)
{
	int ret;
#ifdef HAVE_FIEMAP
	struct fiemap *fiemap;
	unsigned i;

	/* Get extent count */
	fiemap = xzalloc(sizeof(struct fiemap));
	fiemap->fm_length = size;
	ret = ioctl(f, FS_IOC_FIEMAP, fiemap);
	if (ret == -1)
		goto err_out;

	/* Get extents */
	fiemap = xrealloc(fiemap, sizeof(struct fiemap) + fiemap->fm_mapped_extents * sizeof(struct fiemap_extent));
	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	ret = ioctl(f, FS_IOC_FIEMAP, fiemap);
	if (ret == -1)
		goto err_out;

	/* Build extent array */
	*extent_count = fiemap->fm_mapped_extents;
	*extents = xzalloc(*extent_count * sizeof(struct extent));
	for (i = 0; i < *extent_count; i++) {
		(*extents)[i].start = fiemap->fm_extents[i].fe_logical;
		(*extents)[i].end = fiemap->fm_extents[i].fe_logical + fiemap->fm_extents[i].fe_length;
	}

	/* The last extent may extend beyond the end of file, limit it to the actual end */
	if (*extent_count && (*extents)[i-1].end > size)
		(*extents)[i-1].end = size;

	free(fiemap);

	return 0;

err_out:
	ret = -errno;

	free(fiemap);
#else
	ret = -EOPNOTSUPP;
#endif

	/* If failure is due to no filesystem support, return a single extent */
	if (ret == -EOPNOTSUPP || ret == -ENOTTY)
		return whole_file_exent(size, extents, extent_count);

	image_error(image, "fiemap %s: %d %s\n", filename, errno, strerror(errno));
	return ret;
}

/*
 * Write @size @byte bytes at the @offset in @fd. Roughly equivalent to
 * a single "pwrite(fd, big-buffer, size, offset)", except that we try to use
 * more efficient operations (ftruncate and fallocate) if @byte is zero. This
 * only uses methods that do not affect the offset of fd.
 */
static int write_bytes(int fd, size_t size, off_t offset, unsigned char byte)
{
	struct stat st;
	char buf[4096];

	if (!size)
		return 0;

	if (fstat(fd, &st) < 0)
		return -errno;

	if (S_ISREG(st.st_mode) && (byte == 0)) {
		if (offset + size > (size_t)st.st_size) {
			if (ftruncate(fd, offset + size) < 0)
				return -errno;
			/*
			 * The area from st.st_size to offset+size is
			 * zeroed by this operation. If offset was >=
			 * st.st_size, we're done.
			 */
			if (offset >= st.st_size)
				return 0;
			/*
			 * Otherwise, reduce size accordingly, we only
			 * need to write bytes from offset until the
			 * old end of the file.
			 */
			size = st.st_size - offset;
		}
#ifdef HAVE_FALLOCATE
		/*
		 * Use fallocate if it is available and FALLOC_FL_PUNCH_HOLE
		 * is supported by the filesystem. If not, fall through to
		 * the write loop.
		 */
		if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			      offset, size) == 0)
			return 0;
#endif
	}

	/* Not a regular file, non-zero pattern, or fallocate not applicable. */
	memset(buf, byte, sizeof(buf));
	while (size) {
		size_t now = min(size, sizeof(buf));
		int r;

		r = pwrite(fd, buf, now, offset);
		if (r < 0)
			return -errno;
		size -= r;
		offset += r;
	}
	return 0;
}

/*
 * For regular files this makes sure that:
 * - the file exists
 * - the file has the specified size
 * - any previous date is cleared
 * For block devices this makes sure that:
 * - any existing filesystem header is cleared
 */
int prepare_image(struct image *image, unsigned long long size)
{
	if (is_block_device(imageoutfile(image))) {
		insert_image(image, NULL, 2048, 0, 0);
	} else {
		int ret;
		/* for regular files, create the file or truncate it to zero
		 * size to remove all existing content */
		int fd = open_file(image, imageoutfile(image), O_TRUNC);
		if (fd < 0)
			return fd;

		/*
		 * Resize the file immediately to the final size. This is not
		 * strictly necessary but this circumvents XFS preallocation
		 * heuristics. Without this, the holes in the image may be smaller
		 * than necessary.
		 */
		ret = ftruncate(fd, size);
		close(fd);
		if (ret < 0) {
			ret = -errno;
			image_error(image, "failed to truncate %s to %lld: %s\n",
				    imageoutfile(image), size,
				    strerror(-ret));
			return ret;
		}
	}
	return 0;
}

/*
 * Insert the image @sub at offset @offset in @image. If @sub is
 * smaller than @size (including if @sub is NULL), insert @byte bytes for
 * the remainder. If @sub is larger than @size, only the first @size
 * bytes of it will be copied (it's up to the caller to ensure this
 * doesn't happen). This means that after this call, exactly the range
 * [offset, offset+size) in the output @image have been updated.
 */
int insert_image(struct image *image, struct image *sub,
		 unsigned long long size, unsigned long long offset,
		 unsigned char byte)
{
	struct extent *extents = NULL;
	size_t extent_count = 0;
	int fd = -1, in_fd = -1;
	unsigned long long in_pos;
	const char *infile;
	unsigned e;
	int ret;

	fd = open_file(image, imageoutfile(image), 0);
	if (fd < 0) {
		ret = fd;
		goto out;
	}
	if (lseek(fd, offset, SEEK_SET) < 0) {
		ret = -errno;
		goto out;
	}
	if (!sub)
		goto fill;

	infile = imageoutfile(sub);
	in_fd = open(infile, O_RDONLY);
	if (in_fd < 0) {
		ret = -errno;
		image_error(image, "open %s: %s", infile, strerror(errno));
		goto out;
	}
	ret = map_file_extents(image, infile, in_fd, size, &extents, &extent_count);
	if (ret)
		goto out;
	image_debug(image, "copying %llu bytes from %s at offset %llu\n",
		    size, infile, offset);
	in_pos = 0;
	for (e = 0; e < extent_count && size > 0; e++) {
		const struct extent *ext = &extents[e];
		size_t len = ext->start - in_pos;

		/*
		 * If the input file is larger than size, it might
		 * have an extent that starts beyond size.
		 */
		len = min(len, size);
		ret = write_bytes(fd, len, offset, 0); // Assumes 'holes' are always 0 bytes
		if (ret) {
			image_error(image, "writing %zu bytes failed: %s\n", len, strerror(-ret));
			goto out;
		}
		size -= len;
		offset += len;
		in_pos += len;
		while (in_pos < ext->end && size > 0) {
			char buf[4096];
			size_t now;
			int r, w;

			now = min(ext->end - in_pos, sizeof(buf));
			now = min(now, size);
			r = pread(in_fd, buf, now, in_pos);
			if (r < 0) {
				ret = -errno;
				image_error(image, "reading %zu bytes from %s failed: %s\n",
					    now, infile, strerror(errno));
				goto out;
			}
			if (r == 0)
				break;

			w = pwrite(fd, buf, r, offset);
			if (w < r) {
				ret = w < 0 ? -errno : -EIO;
				if (w < 0)
					image_error(image, "write %d bytes: %s\n", r, strerror(errno));
				else
					image_error(image, "short write (%d vs %d)\n", w, r);
				goto out;
			}
			size -= w;
			offset += w;
			in_pos += w;
		}
	}

fill:
	image_debug(image, "adding %llu %#hhx bytes at offset %llu\n",
			size, byte, offset);
	ret = write_bytes(fd, size, offset, byte);
	if (ret)
		image_error(image, "writing %llu bytes failed: %s\n", size, strerror(-ret));

out:
	if (fd >= 0)
		close(fd);
	if (in_fd >= 0)
		close(in_fd);
	free(extents);
	return ret;
}

int insert_data(struct image *image, const void *_data, const char *outfile,
		size_t size, unsigned long long offset)
{
	const char *data = _data;
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

	xasprintf(&uuid, "%04lx%04lx-%04lx-%04lx-%04lx-%04lx%04lx%04lx",
		  random() & 0xffff, random() & 0xffff,
		  random() & 0xffff,
		  (random() & 0x0fff) | 0x4000,
		  (random() & 0x3fff) | 0x8000,
		  random() & 0xffff, random() & 0xffff, random() & 0xffff);

	return uuid;
}

int block_device_size(struct image *image, const char *blkdev, unsigned long long *size)
{
	struct stat st;
	int fd, ret;
	off_t offset;

	fd = open(blkdev, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0) {
		ret = -errno;
		goto out;
	}
	if ((st.st_mode & S_IFMT) != S_IFBLK) {
		ret = -EINVAL;
		goto out;
	}
	offset = lseek(fd, 0, SEEK_END);
	if (offset < 0) {
		ret = -errno;
		goto out;
	}
	*size = offset;
	ret = 0;

out:
	if (ret)
		image_error(image, "failed to determine size of block device %s: %s",
			    blkdev, strerror(-ret));
	if (fd >= 0)
		close(fd);
	return ret;
}

int reload_partitions(struct image *image)
{
#ifdef HAVE_LINUX_FS_H
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
#endif
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

int parse_holes(struct image *image, cfg_t *cfg)
{
	int i;

	if (image->n_holes > 0)
		return 0;

	image->n_holes = cfg ? cfg_size(cfg, "holes") : 0;
	if (image->n_holes == 0)
		return 0;

	image->holes = xzalloc(image->n_holes * sizeof(*image->holes));
	for (i = 0; i < image->n_holes; i++) {
		const char *s = cfg_getnstr(cfg, "holes", i);
		char *start, *end;
		int len;

		if (sscanf(s, " ( %m[0-9skKMG] ; %m[0-9skKMG] ) %n", &start, &end, &len) != 2 ||
		    len != (int)strlen(s)) {
			image_error(image, "invalid hole specification '%s', use '(<start>;<end>)'\n",
				    s);
			return -EINVAL;
		}

		image->holes[i].start = strtoul_suffix(start, NULL, NULL);
		image->holes[i].end = strtoul_suffix(end, NULL, NULL);
		free(start);
		free(end);
		image_debug(image, "added hole (%llu, %llu)\n", image->holes[i].start, image->holes[i].end);
	}
	return 0;
}
