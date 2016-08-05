#ifndef __PTX_IMAGE_H
#define __PTX_IMAGE_H

#include "list.h"

struct image_handler;

struct image *image_get(const char *filename);

int systemp(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void error(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void logmsg(int level, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void image_error(struct image *image, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
void image_log(struct image *image, int level,  const char *fmt, ...) __attribute__ ((format(printf, 3, 4)));

const char *imagepath(void);
const char *inputpath(void);
const char *rootpath(void);
const char *tmppath(void);
const char *mountpath(struct image *);
struct flash_type;

struct mountpoint {
	char *path;
	struct list_head list;
	char *mountpath;
};

struct partition {
	unsigned long long offset;
	unsigned long long size;
	unsigned char partition_type;
	cfg_bool_t bootable;
	cfg_bool_t extended;
	cfg_bool_t read_only;
	const char *image;
	struct list_head list;
	int autoresize;
	int in_partition_table;
	const char *name;
};

struct image {
	const char *name;
	const char *file;
	unsigned long long size;
	const char *mountpoint;
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
};

struct image_handler {
	char *type;
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

extern struct image_handler cpio_handler;
extern struct image_handler ext2_handler;
extern struct image_handler ext3_handler;
extern struct image_handler ext4_handler;
extern struct image_handler file_handler;
extern struct image_handler flash_handler;
extern struct image_handler hdimage_handler;
extern struct image_handler iso_handler;
extern struct image_handler jffs2_handler;
extern struct image_handler rauc_handler;
extern struct image_handler squashfs_handler;
extern struct image_handler tar_handler;
extern struct image_handler ubi_handler;
extern struct image_handler ubifs_handler;
extern struct image_handler vfat_handler;

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
unsigned long long strtoul_suffix(const char *str, char **endp, int base);

int init_config(void);
cfg_opt_t *get_confuse_opts(void);
const char *get_opt(const char *name);
int set_config_opts(int argc, char *argv[], cfg_t *cfg);

enum pad_mode {
	MODE_APPEND,
	MODE_OVERWRITE,
};

int pad_file(struct image *image, const char *infile, const char *outfile,
		size_t size, unsigned char fillpattern, enum pad_mode mode);
int insert_data(struct image *image, const char *data, const char *outfile,
		size_t size, long offset);

unsigned long long cfg_getint_suffix(cfg_t *sec, const char *name);

static inline const char *imageoutfile(const struct image *image)
{
	return image->outfile;
}
#endif /* __PTX_IMAGE_H */
