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
	image-android-sparse.c \
	image-cpio.c \
	image-cramfs.c \
	image-ext2.c \
	image-f2fs.c \
	image-file.c \
	image-fip.c \
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
	$(TESTS) \
	test/test-setup.sh \
	test/cpio.config \
	test/cramfs.config \
	test/exec-check.sh \
	test/exec-fail.config \
	test/exec.config \
	test/ext2.config \
	test/ext2test.0.dump \
	test/ext2test.1.dump \
	test/ext2test.2.dump \
	test/ext2percent.config \
	test/ext2test-percent.0.dump \
	test/ext2test-percent.1.dump \
	test/ext2test-percent.2.dump \
	test/ext3.config \
	test/ext3test.0.dump \
	test/ext3test.1.dump \
	test/ext3test.2.dump \
	test/ext4.config \
	test/ext4test.0.dump \
	test/ext4test.1.dump \
	test/ext4test.2.dump \
	test/f2fs.config \
	test/fip.config \
	test/fit.its \
	test/fit.config \
	test/flash-types.config \
	test/flash.config \
	test/flash.md5 \
	test/gpt-overlap1.config \
	test/gpt-overlap2.config \
	test/gpt-overlap3.config \
	test/gpt-partition-types.config \
	test/gpt-partition-types.fdisk \
	test/gpt-invalid-partition-type1.config \
	test/gpt-invalid-partition-type2.config \
	test/hdimage.config \
	test/hdimage2.config \
	test/hdimage.fdisk \
	test/hdimage.fdisk-2 \
	test/hdimage4.config \
	test/hdimage4.fdisk \
	test/hdimage5.config \
	test/hdimage5.fdisk \
	test/hdimage6.config \
	test/hdimage6.fdisk \
	test/hdimage7.config \
	test/hdimage7.fdisk \
	test/hole.config \
	test/hdimage-hybrid.config \
	test/hdimage-hybrid.fdisk \
	test/hdimage-fail1.config \
	test/hdimage-fail2.config \
	test/hdimage-fail3.config \
	test/hdimage-fail4.config \
	test/hdimage-fail5.config \
	test/hdimage-fail6.config \
	test/hdimage-fail7.config \
	test/hdimage-fail8.config \
	test/hdimage-fail9.config \
	test/hdimage-fail10.config \
	test/hdimage-fail11.config \
	test/hdimage-nopart.config \
	test/hdimage-nopart.hexdump \
	test/hdimage-forced-primary.config \
	test/hdimage-forced-primary.fdisk \
	test/include-aaa.fdisk \
	test/include-bbb.fdisk \
	test/include-ccc.fdisk \
	test/include-test.config \
	test/include.config \
	test/include/aaa/include-test.config \
	test/include/bbb/include-test.config \
	test/iso.config \
	test/jffs2.config \
	test/jffs2.md5 \
	test/mke2fs.conf \
	test/mke2fs.config \
	test/mke2fs.0.dump \
	test/mke2fs.1.dump \
	test/mke2fs.2.dump \
	test/mke2fs.3.dump \
	test/qemu.config \
	test/qemu.qcow.gz \
	test/rauc-openssl-ca/ca.cert.pem \
	test/rauc-openssl-ca/rauc.cert.pem \
	test/rauc-openssl-ca/rauc.key.pem \
	test/rauc.config \
	test/sharness.sh \
	test/sparse.config \
	test/squashfs.config \
	test/tar.config \
	test/test.raucb.info.1 \
	test/test.raucb.info.2 \
	test/test.raucb.info.3 \
	test/test.raucb.info.4 \
	test/test2.raucb.info.1 \
	test/test2.raucb.info.2 \
	test/test2.raucb.info.3 \
	test/test2.raucb.info.4 \
	test/ubi.config \
	test/ubifs.config \
	test/vfat.config



TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
                  $(top_srcdir)/build-aux/tap-driver.sh
TEST_LOG_COMPILER = fakeroot

TESTS = \
	test/genimage.test \
	test/ext.test \
	test/filesystem.test \
	test/flash.test \
	test/hdimage.test \
	test/misc.test

# when "make clean" runs
CLEANFILES = \
	test-results/*.test*.counts

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
