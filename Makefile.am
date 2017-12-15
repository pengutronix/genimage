if BUILD_SILENTLY
AM_MAKEFLAGS = --no-print-directory
endif

EXTRA_DIST = \
	test.config \
	flash.conf

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h

bin_PROGRAMS = genimage
genimage_SOURCES = \
	genimage.c \
	config.c \
	util.c \
	image-cpio.c \
	image-cramfs.c \
	image-ext2.c \
	image-file.c \
	image-flash.c \
	image-hd.c \
	image-iso.c \
	image-jffs2.c \
	image-rauc.c \
	image-squashfs.c \
	image-tar.c \
	image-ubi.c \
	image-ubifs.c \
	image-vfat.c

genimage_CFLAGS = \
	$(AM_CFLAGS) \
	$(CONFUSE_CFLAGS)

genimage_LDADD = \
	$(CONFUSE_LIBS)

noinst_HEADERS = \
	genimage.h \
	list.h

EXTRA_DIST += \
	test/basic-images.test \
	test/cpio.config \
	test/cramfs.config \
	test/ext2.config \
	test/ext2test.dump \
	test/ext3.config \
	test/ext3test.dump \
	test/ext4.config \
	test/ext4test.dump \
	test/flash-types.config \
	test/flash.config \
	test/flash.md5 \
	test/hdimage.config \
	test/hdimage.fdisk \
	test/hdimage.fdisk-2 \
	test/iso.config \
	test/jffs2.config \
	test/jffs2.md5 \
	test/sharness.sh \
	test/squashfs.config \
	test/tar.config \
	test/ubi.config \
	test/ubifs.config \
	test/vfat.config


TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
                  $(top_srcdir)/build-aux/tap-driver.sh
TEST_LOG_COMPILER = fakeroot

TESTS = \
	test/basic-images.test

# when "make clean" runs
CLEANFILES = \
	test-results/basic-images.test*.counts

# when "make distclean" runs
DISTCLEAN = \
	Makefile

# when "make maintainer-clean" runs
MAINTAINERCLEANFILES = \
	configure \
	autoscan.log \
	config.h.in~ \
	config.h.in \
	configure.scan \
	configure.ac~ \
	aclocal.m4 \
	Makefile.in \
	build-aux/depcomp \
	build-aux/install-sh \
	build-aux/missing \
	$(DIST_ARCHIVES)
