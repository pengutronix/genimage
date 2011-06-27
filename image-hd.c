#define _GNU_SOURCE
#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "genimage.h"

static int hdimage_generate(struct image *image)
{
	struct partition *part;
	enum pad_mode mode = MODE_OVERWRITE;
	int ret;

	list_for_each_entry(part, &image->partitions, list) {
		struct image *child;
		char *infile, *outfile;

		child = image_get(part->image);
		if (!child) {
			image_error(image, "could not find %s\n", part->image);
			return -EINVAL;
		}

		asprintf(&infile, "%s/%s", imagepath(), part->image);
		asprintf(&outfile, "%s", imageoutfile(image));

		if (part->offset) {
			ret = pad_file(NULL, outfile, part->offset, 0x0, mode);
			if (ret) {
				image_error(image, "failed to pad image to size %lld\n",
						part->offset);
				free(infile);
				free(outfile);
				return ret;
			}
			mode = MODE_APPEND;
		}

		ret = pad_file(infile, outfile, part->size, 0x0, mode);

		free(infile);
		free(outfile);

		if (ret)
			return ret;
		mode = MODE_APPEND;
	}

	return 0;
}

static int hdimage_setup(struct image *image, cfg_t *cfg)
{
	struct partition *part;
	unsigned long long now = 0;

	list_for_each_entry(part, &image->partitions, list) {
		if (part->offset) {
			if (now > part->offset) {
				image_error(image, "part %s overlaps with previous partition\n",
						part->name);
				return -EINVAL;
			}
			now = part->offset + part->size;		
		} else {
			now = now + part->size;
		}
	}

	if (now > image->size) {
		image_error(image, "partitions exceed device size\n");
		return -EINVAL;
	}
	return 0;
}

cfg_opt_t hdimage_opts[] = {
	CFG_END()
};

struct image_handler hdimage_handler = {
	.type = "hdimage",
	.generate = hdimage_generate,
	.setup = hdimage_setup,
	.opts = hdimage_opts,
};

