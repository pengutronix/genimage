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

# ----------- autodetect some settings -------------------

# add as much warnings and features as possible, but check what the compiler
# is able to understand and use it only if possible

CC_CHECK_CFLAGS_SILENT([-pipe],[AM_CFLAGS="${AM_CFLAGS} -pipe"])
CC_CHECK_CFLAGS_SILENT([-Wall],[AM_CFLAGS="${AM_CFLAGS} -Wall"])
CC_CHECK_CFLAGS_SILENT([-Wextra],[AM_CFLAGS="${AM_CFLAGS} -Wextra"])
CC_CHECK_CFLAGS_SILENT([-Wmissing-declarations],[AM_CFLAGS="${AM_CFLAGS} -Wmissing-declarations"])
CC_CHECK_CFLAGS_SILENT([-Wmissing-prototypes],[AM_CFLAGS="${AM_CFLAGS} -Wmissing-prototypes"])
CC_CHECK_CFLAGS_SILENT([-Wnested-externs],[AM_CFLAGS="${AM_CFLAGS} -Wnested-externs"])
CC_CHECK_CFLAGS_SILENT([-Wpointer-arith],[AM_CFLAGS="${AM_CFLAGS} -Wpointer-arith"])
CC_CHECK_CFLAGS_SILENT([-Wsign-compare],[AM_CFLAGS="${AM_CFLAGS} -Wsign-compare"])
CC_CHECK_CFLAGS_SILENT([-Wchar-subscripts],[AM_CFLAGS="${AM_CFLAGS} -Wchar-subscripts"])
CC_CHECK_CFLAGS_SILENT([-Wstrict-prototypes],[AM_CFLAGS="${AM_CFLAGS} -Wstrict-prototypes"])
CC_CHECK_CFLAGS_SILENT([-Wshadow],[AM_CFLAGS="${AM_CFLAGS} -Wshadow"])
CC_CHECK_CFLAGS_SILENT([-Wformat-security],[AM_CFLAGS="${AM_CFLAGS} -Wformat-security"])
CC_CHECK_CFLAGS_SILENT([-Wtype-limits],[AM_CFLAGS="${AM_CFLAGS} -Wtype-limits"])
CC_CHECK_CFLAGS_SILENT([-ffunction-sections],[AM_CFLAGS="${AM_CFLAGS} -ffunction-sections"])
CC_CHECK_CFLAGS_SILENT([-fdata-sections],[AM_CFLAGS="${AM_CFLAGS} -fdata-sections"])

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
        cflags:                 ${CFLAGS} ${AM_CFLAGS}
        ldflags:                ${LDFLAGS} ${AM_LDFLAGS}

        debug:                  ${enable_debug}
])
