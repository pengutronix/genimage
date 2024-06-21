#ifndef __PTX_IMAGE_H
#define __PTX_IMAGE_H

#include <stdint.h>
#include <confuse.h>
#include "list.h"

struct image_handler;

struct image *image_get(const char *filename);

int systemp(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void error(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void info(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void debug(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void image_error(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void image_info(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void image_debug(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void xasprintf(char **strp, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void xstrcatf(char **strp, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

void disable_rootpath(void);
const char *imagepath(void);
const char *inputpath(void);
const char *rootpath(void);
const char *tmppath(void);
const char *mountpath(const struct image *);
struct flash_type;

struct mountpoint {
	char *path;
	struct list_head list;
	char *mountpath;
};

struct partition {
	unsigned long long offset;
	unsigned long long size;
	unsigned long long align;
	unsigned char partition_type;
	cfg_bool_t bootable;
	cfg_bool_t logical;
	cfg_bool_t forced_primary;
	cfg_bool_t read_only;
	cfg_bool_t hidden;
	cfg_bool_t no_automount;
	cfg_bool_t fill;
	const char *image;
	off_t imageoffset;
	struct list_head list;
	int autoresize;
	int in_partition_table;
	const char *name;
	const char *partition_type_uuid;
	const char *partition_uuid;
	cfg_t *cfg;
};

struct image {
	const char *name;
	const char *file;
	unsigned long long size;
	struct extent *holes;
	int n_holes;
	cfg_bool_t size_is_percent;
	const char *mountpoint;
	const char *srcpath;
	cfg_bool_t empty;
	cfg_bool_t temporary;
	const char *exec_pre;
	const char *exec_post;
	unsigned char partition_type;
	void *handler_priv;
	struct image_handler *handler;
	struct list_head list;
	int done;
	struct flash_type *flash_type;
	cfg_t *imagesec;
	struct list_head partitions;
	struct mountpoint *mp;
	char *outfile;
	int seen;
	off_t last_offset;
};

struct image_handler {
	char *type;
	cfg_bool_t no_rootpath;
	int (*parse)(struct image *i, cfg_t *cfg);
	int (*setup)(struct image *i, cfg_t *cfg);
	int (*generate)(struct image *i);
	cfg_opt_t *opts;
};

struct flash_type {
	const char *name;
	int pebsize;
	int lebsize;
	int numpebs;
	int minimum_io_unit_size;
	int vid_header_offset;
	int sub_page_size;
	struct list_head list;
};

struct flash_type *flash_type_get(const char *name);

extern struct image_handler android_sparse_handler;
extern struct image_handler cpio_handler;
extern struct image_handler cramfs_handler;
extern struct image_handler ext2_handler;
extern struct image_handler ext3_handler;
extern struct image_handler ext4_handler;
extern struct image_handler f2fs_handler;
extern struct image_handler file_handler;
extern struct image_handler flash_handler;
extern struct image_handler hdimage_handler;
extern struct image_handler iso_handler;
extern struct image_handler jffs2_handler;
extern struct image_handler qemu_handler;
extern struct image_handler rauc_handler;
extern struct image_handler squashfs_handler;
extern struct image_handler tar_handler;
extern struct image_handler ubi_handler;
extern struct image_handler ubifs_handler;
extern struct image_handler vfat_handler;
extern struct image_handler fit_handler;
extern struct image_handler fip_handler;

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

void *xzalloc(size_t n);
void *xrealloc(void *ptr, size_t size);
unsigned long long strtoul_suffix(const char *str, char **endp,
		cfg_bool_t *percent);

int init_config(void);
cfg_opt_t *get_confuse_opts(void);
const char *get_opt(const char *name);
int set_config_opts(int argc, char *argv[], cfg_t *cfg);

static inline size_t min(size_t a, size_t b)
{
	return a < b ? a : b;
}

enum pad_mode {
	MODE_APPEND,
	MODE_OVERWRITE,
};

struct extent {
	unsigned long long start, end;
};

int open_file(struct image *image, const char *filename, int extra_flags);
int map_file_extents(struct image *image, const char *filename, int fd,
		     size_t size, struct extent **extents, size_t *extent_count);
int is_block_device(const char *filename);
int block_device_size(struct image *image, const char *blkdev,
		      unsigned long long *size);
int prepare_image(struct image *image, unsigned long long size);
int insert_image(struct image *image, struct image *sub,
		 unsigned long long size, unsigned long long offset,
		 unsigned char byte);
int insert_data(struct image *image, const void *data, const char *outfile,
		size_t size, unsigned long long offset);
int extend_file(struct image *image, size_t size);
int reload_partitions(struct image *image);
int parse_holes(struct image *image, cfg_t *cfg);

unsigned long long cfg_getint_suffix(cfg_t *sec, const char *name);
unsigned long long cfg_getint_suffix_percent(cfg_t *sec, const char *name,
		cfg_bool_t *percent);

static inline const char *imageoutfile(const struct image *image)
{
	return image->outfile;
}

char *sanitize_path(const char *path);

int uuid_validate(const char *str);
void uuid_parse(const char *str, unsigned char *uuid);
char *uuid_random(void);

unsigned long long image_dir_size(struct image *image);

uint32_t crc32(const void *data, size_t len);
uint32_t crc32_next(const void *data, size_t len, uint32_t last_crc);

#define ct_assert(e) _Static_assert(e, #e)

#endif /* __PTX_IMAGE_H */
