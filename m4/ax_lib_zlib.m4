# SYNOPSIS
#
#   AX_LIB_ZLIB()
#
# DESCRIPTION
#
#   This macro provides tests of availability of the zlib
#   compression library. This macro checks for zlib
#   headers and libraries and defines compilation flags
#
#   Macro supports following options and their values:
#
#   1) Single-option usage:
#
#     --with-zlib      -- yes, no, or path to zlib library 
#                          installation prefix
#
#   2) Three-options usage (all options are required):
#
#     --with-zlib=yes
#     --with-zlib-inc  -- path to base directory with zlib headers
#     --with-zlib-lib  -- linker flags for zlib
#
#   This macro calls:
#
#     AC_SUBST(ZLIB_CFLAGS)
#     AC_SUBST(ZLIB_LDFLAGS)
#     AC_SUBST(ZLIB_LIBS)
#
#   And sets:
#
#     HAVE_ZLIB
#
# LICENSE
#
#   Copyright (c) 2009 Hartmut Holzgraefe <hartmut@php.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_LIB_ZLIB],
[
    AC_ARG_WITH([zlib],
        AC_HELP_STRING([--with-zlib=@<:@ARG@:>@],
            [use zlib library from given prefix (ARG=path); check standard prefixes (ARG=yes); disable (ARG=no)]
        ),
        [
        if test "$withval" = "yes"; then
            if test -f /usr/local/include/zlib.h ; then
                zlib_prefix=/usr/local
            elif test -f /usr/include/zlib.h ; then
                zlib_prefix=/usr
            else
                zlib_prefix=""
            fi
            zlib_requested="yes"
        elif test -d "$withval"; then
            zlib_prefix="$withval"
            zlib_requested="yes"
        else
            zlib_prefix=""
            zlib_requested="no"
        fi
        ],
        [
        dnl Default behavior is implicit yes
        if test -f /usr/local/include/zlib.h ; then
            zlib_prefix=/usr/local
        elif test -f /usr/include/zlib.h ; then
            zlib_prefix=/usr
        else
            zlib_prefix=""
        fi
        ]
    )

    AC_ARG_WITH([zlib-inc],
        AC_HELP_STRING([--with-zlib-inc=@<:@DIR@:>@],
            [path to zlib library headers]
        ),
        [zlib_include_dir="$withval"],
        [zlib_include_dir=""]
    )
    AC_ARG_WITH([zlib-lib],
        AC_HELP_STRING([--with-zlib-lib=@<:@ARG@:>@],
            [link options for zlib library]
        ),
        [zlib_lib_flags="$withval"],
        [zlib_lib_flags=""]
    )

    ZLIB_CFLAGS=""
    ZLIB_LDFLAGS=""

    dnl
    dnl Collect include/lib paths and flags
    dnl
    run_zlib_test="no"

    if test -n "$zlib_prefix"; then
        zlib_include_dir="$zlib_prefix/include"
        zlib_lib_flags="-L$zlib_prefix/lib"
        zlib_lib_libs="-lz"
        run_zlib_test="yes"
    elif test "$zlib_requested" = "yes"; then
        if test -n "$zlib_include_dir" -a -n "$zlib_lib_flags" -a -n "$zlib_lib_libs"; then
            run_zlib_test="yes"
        fi
    else
        run_zlib_test="no"
    fi

    dnl
    dnl Check zlib files
    dnl
    if test "$run_zlib_test" = "yes"; then

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$zlib_include_dir"

        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $zlib_lib_flags"

        saved_LIBS="$LIBS"
        LIBS="$LIBS $zlib_lib_libs"

        dnl
        dnl Check zlib headers
        dnl
        AC_MSG_CHECKING([for zlib headers in $zlib_include_dir])

        AC_LANG_PUSH([C++])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM(
                [[
@%:@include <zlib.h>
                ]],
                [[]]
            )],
            [
            ZLIB_CFLAGS="-I$zlib_include_dir"
            zlib_header_found="yes"
            AC_MSG_RESULT([found])
            ],
            [
            zlib_header_found="no"
            AC_MSG_RESULT([not found])
            ]
        )
        AC_LANG_POP([C++])

        dnl
        dnl Check zlib libraries
        dnl
        if test "$zlib_header_found" = "yes"; then

            AC_MSG_CHECKING([for zlib library])

            AC_LANG_PUSH([C++])
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM(
                    [[
@%:@include <zlib.h>
                    ]],
                    [[
    const char *version;
    
    version = zlibVersion();
                    ]]
                )],
                [
                ZLIB_LDFLAGS="$zlib_lib_flags"
                ZLIB_LIBS="$zlib_lib_libs"
                zlib_lib_found="yes"
                AC_MSG_RESULT([found])
                ],
                [
                zlib_lib_found="no"
                AC_MSG_RESULT([not found])
                ]
            )
            AC_LANG_POP([C++])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
        LIBS="$saved_LIBS"
    fi

    AC_MSG_CHECKING([for zlib compression library])

    if test "$run_zlib_test" = "yes"; then
        if test "$zlib_header_found" = "yes" -a "$zlib_lib_found" = "yes"; then
            AC_SUBST([ZLIB_CFLAGS])
            AC_SUBST([ZLIB_LDFLAGS])
            AC_SUBST([ZLIB_LIBS])
            AC_SUBST([HAVE_ZLIB])

            AC_DEFINE([HAVE_ZLIB], [1],
                [Define to 1 if zlib library is available])

            HAVE_ZLIB="yes"
        else
            HAVE_ZLIB="no"
        fi

        AC_MSG_RESULT([$HAVE_ZLIB])


    else
        HAVE_ZLIB="no"
        AC_MSG_RESULT([$HAVE_ZLIB])

        if test "$zlib_requested" = "yes"; then
            AC_MSG_WARN([zlib compression support requested but headers or library not found. Specify valid prefix of zlib using --with-zlib=@<:@DIR@:>@ or provide include directory and linker flags using --with-zlib-inc and --with-zlib-lib])
        fi
    fi
])

