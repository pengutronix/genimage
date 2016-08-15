AC_PREREQ(2.60)
AC_INIT([genimage],
        [9],
        [s.hauer@pengutronix.de],
        [genimage],
        [http://www.pengutronix.de/genimage/])
AC_CONFIG_SRCDIR([genimage.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects])
AC_PROG_CC_STDC

# If possible, enable extensions to Posix
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# where to put extension macros
AC_CONFIG_MACRO_DIR([m4])

# default is less output while building the package
AM_SILENT_RULES([yes])

# change if 'usr/local' as the default install path isn't a good choice
#AC_PREFIX_DEFAULT([/usr/local])

AC_CHECK_FUNCS([memset setenv strdup strcasecmp strerror strstr strtoull])

AC_C_INLINE
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

# ----------- query user's settings ----------------------
AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE([debug],
	AS_HELP_STRING([--enable-debug],
		[enable debug messages @<:@default=disabled@:>@]),
	[],
	[enable_debug=no])
AC_MSG_RESULT([${enable_debug}])

# should the executable export all symbols?
AC_MSG_CHECKING([whether to hide internal symbols])
AC_ARG_ENABLE([hide],
	[AS_HELP_STRING([--disable-hide],
		[do not hide all internal symbols @<:@default=enabled@:>@])],
	[],
	[enable_hide=yes])
# for debugging purposes we must disable the hiding feature
AS_IF([test "x${enable_debug}" = "xyes"],
	[AC_MSG_RESULT([no (due to debug enabled)])
	enable_hide=no],
	[AC_MSG_RESULT([${enable_hide}])])

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
CC_CHECK_CFLAGS_SILENT([-Wunused-parameter],[AM_CFLAGS="${AM_CFLAGS} -Wno-unused-parameter"])
CC_CHECK_CFLAGS_SILENT([-ffunction-sections],[AM_CFLAGS="${AM_CFLAGS} -ffunction-sections"])
CC_CHECK_CFLAGS_SILENT([-fdata-sections],[AM_CFLAGS="${AM_CFLAGS} -fdata-sections"])

# Add only those libraries which are really used
CC_CHECK_LDFLAGS([-Wl,--as-needed], [AM_LDFLAGS="${AM_LDFLAGS} -Wl,--as-needed"],[])
CC_CHECK_LDFLAGS([-Wl,--gc-sections], [AM_LDFLAGS="${AM_LDFLAGS} -Wl,--gc-sections"],[])

PKG_CHECK_MODULES(CONFUSE, libconfuse)

# ------- use all the settings ----------------------

AS_IF([test "x$enable_debug" = "xyes"], [
        AC_DEFINE(ENABLE_DEBUG, [1], [Debug messages.])
])

# Enable "-fvisibility=hidden" only if the used gcc supports it
AS_IF([test "${enable_hide}" = "yes"],
	[AC_MSG_CHECKING([whether the compiler supports -fvisibility=hidden])
	CC_CHECK_CFLAGS_SILENT([-fvisibility=hidden], [], [enable_hide=no])
	# still enabled?
	if test "x${enable_hide}" = "xyes"; then
		AC_DEFINE(DSO_HIDDEN, 1, [hide internal library symbols])
		AM_CFLAGS="${AM_CFLAGS} -fvisibility=hidden"
	fi

	AC_MSG_RESULT([${enable_hide}])])

AC_SUBST(AM_CFLAGS)

# be very silent on request
AM_CONDITIONAL(BUILD_SILENTLY, test "x$AM_DEFAULT_VERBOSITY" = x0)

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
        hide symbols:           ${enable_hide}
        libconfuse:             ${CONFUSE_LIBS}
])
