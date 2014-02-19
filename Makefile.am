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
	image-ext2.c \
	image-file.c \
	image-flash.c \
	image-hd.c \
	image-iso.c \
	image-jffs2.c \
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

# when "make clean" runs
CLEANFILES =

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
