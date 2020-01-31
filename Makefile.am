if BUILD_SILENTLY
AM_MAKEFLAGS = --no-print-directory
endif

EXTRA_DIST = \
	README.rst \
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
	crc32.c \
	image-cpio.c \
	image-cramfs.c \
	image-ext2.c \
	image-file.c \
	image-fit.c \
	image-flash.c \
	image-hd.c \
	image-iso.c \
	image-jffs2.c \
	image-qemu.c \
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
	test/ext2percent.config \
	test/ext2test-percent.dump \
	test/ext3.config \
	test/ext3test.dump \
	test/ext4.config \
	test/ext4test.dump \
	test/fit.its \
	test/fit.config \
	test/flash-types.config \
	test/flash.config \
	test/flash.md5 \
	test/hdimage.config \
	test/hdimage2.config \
	test/hdimage.fdisk \
	test/hdimage.fdisk-2 \
	test/hdimage4.config \
	test/hdimage4.fdisk \
	test/iso.config \
	test/jffs2.config \
	test/jffs2.md5 \
	test/mke2fs.conf \
	test/mke2fs.config \
	test/mke2fs.dump \
	test/qemu.config \
	test/qemu.qcow.gz \
	test/rauc-openssl-ca/ca.cert.pem \
	test/rauc-openssl-ca/rauc.cert.pem \
	test/rauc-openssl-ca/rauc.key.pem \
	test/rauc.config \
	test/sharness.sh \
	test/squashfs.config \
	test/tar.config \
	test/test.raucb.info \
	test/test2.raucb.info \
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
