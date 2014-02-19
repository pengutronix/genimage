AC_PREREQ(2.60)
AC_INIT([genimage],
        [5],
        [s.hauer@pengutronix.de],
        [genimage],
        [http://www.pengutronix.de/genimage/])
AC_CONFIG_SRCDIR([genimage.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects])
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT([disable-static pic-only])
AC_PREFIX_DEFAULT([/usr])

AC_PROG_MKDIR_P

# ----------- query user's settings ----------------------
AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE([debug],
	AS_HELP_STRING([--enable-debug],
		[enable debug messages @<:@default=disabled@:>@]),
	[],
	[enable_debug=no])
AC_MSG_RESULT([${enable_debug}])

my_CFLAGS="-Wall \
-Wmissing-declarations -Wmissing-prototypes \
-Wnested-externs -Wpointer-arith \
-Wpointer-arith -Wsign-compare -Wchar-subscripts \
-Wstrict-prototypes -Wshadow \
-Wformat-security -Wtype-limits"
AC_SUBST([my_CFLAGS])

PKG_CHECK_MODULES(CONFUSE, libconfuse)

# ------- use all the settings ----------------------

AS_IF([test "x$enable_debug" = "xyes"], [
        AC_DEFINE(ENABLE_DEBUG, [1], [Debug messages.])
])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([
        Makefile
])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE $VERSION
        =====

        prefix:                 ${prefix}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}

        debug:                  ${enable_debug}
])
