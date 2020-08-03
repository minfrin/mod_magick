#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(mod_magick, 1.0.0, minfrin@sharp.fm)
AM_INIT_AUTOMAKE([dist-bzip2])
AC_CONFIG_FILES([Makefile mod_magick.spec])
AC_CONFIG_SRCDIR([mod_magick.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_ARG_WITH(apxs,
    [  --with-apxs=PATH        path to Apache apxs],
    [
        if test "$withval" = "yes"; then
            AC_CHECK_PROGS(APXS, apxs /usr/sbin/apxs, reject)
        else
            APXS=$withval
            AC_SUBST(APXS)
        fi
    ],
    [
        AC_CHECK_PROGS(APXS, apxs /usr/sbin/apxs, reject)
    ])
if test "$APXS" = "reject"; then
  AC_MSG_ERROR([Could not find apxs on the path.])
fi

# Make sure the Apache include files are found
AM_CPPFLAGS="$AM_CPPFLAGS -I`$APXS -q INCLUDEDIR`"
AM_CFLAGS="$AM_CFLAGS -I`$APXS -q INCLUDEDIR`"

AC_ARG_WITH(install,
    [  --with-install=PATH     path to install],
    [
        if test "$withval" = "yes"; then
            AC_CHECK_PROGS(INSTALL, install /usr/sbin/install, reject)
        else
            APXS=$withval
            AC_SUBST(INSTALL)
        fi
    ],
    [
        AC_CHECK_PROGS(INSTALL, install /usr/sbin/install, reject)
    ])
if test "$INSTALL" = "reject"; then
  AC_MSG_ERROR([Could not find install on the path.])
fi

# Checks for libraries.
PKG_CHECK_MODULES(GraphicsMagick, GraphicsMagick >= 1.3.0)
PKG_CHECK_MODULES(GraphicsMagickWand, GraphicsMagickWand >= 1.3.0)
PKG_CHECK_MODULES(apr, apr-1 >= 1.3)
PKG_CHECK_MODULES(apu, apr-util-1 >= 1.3)

CFLAGS="$CFLAGS $GraphicsMagick_CFLAGS $GraphicsMagickWand_CFLAGS $apr_CFLAGS $apu_CFLAGS"
CPPFLAGS="$CPPFLAGS $GraphicsMagick_CFLAGS $GraphicsMagickWand_CPPFLAGS $apr_CPPFLAGS $apu_CPPFLAGS"
LDFLAGS="$LDFLAGS $GraphicsMagick_LIBS $GraphicsMagickWand_LIBS $apr_LIBS $apu_LIBS"

# Checks for header files.
AC_CHECK_HEADERS([wand/wand_api.h], , AC_MSG_ERROR([wand/wand_api.h was not found.]))

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_LIB(GraphicsMagick, MagickReadImage, , AC_MSG_ERROR([GraphicsMagick was not found.]))
AC_CHECK_LIB(GraphicsMagickWand, MagickSetImageFormat, , AC_MSG_ERROR([MagickSetImageFormat was not found.]))

AC_SUBST(PACKAGE_VERSION)
AC_OUTPUT
