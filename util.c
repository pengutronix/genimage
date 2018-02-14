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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "genimage.h"

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
unsigned long long strtoul_suffix(const char *str, char **endp, int base)
{
	unsigned long long val;
	char *end;

	val = strtoull(str, &end, base);

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

int pad_file(struct image *image, const char *infile, const char *outfile,
		size_t size, unsigned char fillpattern, enum pad_mode mode)
{
	FILE *f = NULL, *outf = NULL;
	void *buf = NULL;
	int now, r, w;
	int ret = 0;

	if (infile) {
		f = fopen(infile, "r");
		if (!f) {
			ret = -errno;
			image_error(image, "open %s: %s\n", infile, strerror(errno));
			goto err_out;
		}
	}

	outf = fopen(outfile, mode == MODE_OVERWRITE ? "w" : "a");
	if (!outf) {
		ret = -errno;
		image_error(image, "open %s: %s\n", outfile, strerror(errno));
		goto err_out;
	}

	buf = xzalloc(4096);

	if (!infile) {
		struct stat s;
		ret = stat(outfile, &s);
		if (ret)
			goto err_out;
		if ((unsigned long long)s.st_size > size) {
			image_error(image, "input file '%s' too large\n", outfile);
			ret = -EINVAL;
			goto err_out;
		}
		size = size - s.st_size;
		goto fill;
	}

	while (size) {
		now = min(size, 4096);

		r = fread(buf, 1, now, f);
		w = fwrite(buf, 1, r, outf);
		if (w < r) {
			ret = -errno;
			image_error(image, "write %s: %s\n", outfile, strerror(errno));
			goto err_out;
		}
		size -= r;

		if (r < now)
			goto fill;
	}

	now = fread(buf, 1, 1, f);
	if (now == 1) {
		image_error(image, "input file '%s' too large\n", infile);
		ret = -EINVAL;
		goto err_out;
	}

fill:
	memset(buf, fillpattern, 4096);

	while (size) {
		now = min(size, 4096);

		r = fwrite(buf, 1, now, outf);
		if (r < now) {
			ret = -errno;
			image_error(image, "write %s: %s\n", outfile, strerror(errno));
			goto err_out;
		}
		size -= now;
	}
err_out:
	free(buf);
	if (f)
		fclose(f);
	if (outf)
		fclose(outf);

	return ret;
}

int insert_data(struct image *image, const char *data, const char *outfile,
		size_t size, long offset)
{
	FILE *outf = NULL;
	int now, r;
	int ret = 0;

	outf = fopen(outfile, "r+");
	if (!outf && errno == ENOENT)
		outf = fopen(outfile, "w");
	if (!outf) {
		ret = -errno;
		image_error(image, "open %s: %s\n", outfile, strerror(errno));
		goto err_out;
	}
	ret = fseek(outf, offset, SEEK_SET);
	if (ret) {
		ret = -errno;
		image_error(image, "seek %s: %s\n", outfile, strerror(errno));
		goto err_out;
	}
	while (size) {
		now = min(size, 4096);

		r = fwrite(data, 1, now, outf);
		if (r < now) {
			ret = -errno;
			image_error(image, "write %s: %s\n", outfile, strerror(errno));
			goto err_out;
		}
		size -= now;
		data += now;
	}
err_out:
	if (outf)
		fclose(outf);

	return ret;
}
