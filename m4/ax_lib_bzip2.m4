# SYNOPSIS
#
#   AX_LIB_BZIP2()
#
# DESCRIPTION
#
#   This macro provides tests of availability of the bzip2
#   compression library. This macro checks for bzip2
#   headers and libraries and defines compilation flags
#
#   Macro supports following options and their values:
#
#   1) Single-option usage:
#
#     --with-bzip2      -- yes, no, or path to bzip2 library 
#                          installation prefix
#
#   2) Three-options usage (all options are required):
#
#     --with-bzip2=yes
#     --with-bzip2-inc  -- path to base directory with bzip2 headers
#     --with-bzip2-lib  -- linker flags for bzip2
#
#   This macro calls:
#
#     AC_SUBST(BZIP2_CFLAGS)
#     AC_SUBST(BZIP2_LDFLAGS)
#     AC_SUBST(BZIP2_LiBS)
#
#   And sets:
#
#     HAVE_BZIP2
#
# LICENSE
#
#   Copyright (c) 2009 Hartmut Holzgraefe <hartmut@php.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_LIB_BZIP2],
[
    AC_ARG_WITH([bzip2],
        AC_HELP_STRING([--with-bzip2=@<:@ARG@:>@],
            [use bzip2 library from given prefix (ARG=path); check standard prefixes (ARG=yes); disable (ARG=no)]
        ),
        [
        if test "$withval" = "yes"; then
            if test -f /usr/local/include/bzlib.h ; then
                bzlib_prefix=/usr/local
            elif test -f /usr/include/bzlib.h ; then
                bzlib_prefix=/usr
            else
                bzlib_prefix=""
            fi
            bzlib_requested="yes"
        elif test -d "$withval"; then
            bzlib_prefix="$withval"
            bzlib_requested="yes"
        else
            bzlib_prefix=""
            bzlib_requested="no"
        fi
        ],
        [
        dnl Default behavior is implicit yes
        if test -f /usr/local/include/bzlib.h ; then
            bzlib_prefix=/usr/local
        elif test -f /usr/include/bzlib.h ; then
            bzlib_prefix=/usr
        else
            bzlib_prefix=""
        fi
        ]
    )

    AC_ARG_WITH([bzip2-inc],
        AC_HELP_STRING([--with-bzip2-inc=@<:@DIR@:>@],
            [path to bzip2 library headers]
        ),
        [bzlib_include_dir="$withval"],
        [bzlib_include_dir=""]
    )
    AC_ARG_WITH([bzip2-lib],
        AC_HELP_STRING([--with-bzip2-lib=@<:@ARG@:>@],
            [link options for bzip2 library]
        ),
        [bzlib_lib_flags="$withval"],
        [bzlib_lib_flags=""]
    )

    BZIP2_CFLAGS=""
    BZIP2_LDFLAGS=""
    BZIP2_LIBS=""

    dnl
    dnl Collect include/lib paths and flags
    dnl
    run_bzlib_test="no"

    if test -n "$bzlib_prefix"; then
        bzlib_include_dir="$bzlib_prefix/include"
        bzlib_lib_flags="-L$bzlib_prefix/lib"
        bzlib_lib_libs="-lbz2"
        run_bzlib_test="yes"
    elif test "$bzlib_requested" = "yes"; then
        if test -n "$bzlib_include_dir" -a -n "$bzlib_lib_flags" -a -n "$bzlib_lib_libs"; then
            run_bzlib_test="yes"
        fi
    else
        run_bzlib_test="no"
    fi

    dnl
    dnl Check bzip2 files
    dnl
    if test "$run_bzlib_test" = "yes"; then

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$bzlib_include_dir"

        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $bzlib_lib_flags"

        saved_LIBSS="$LIBS"
        LIBS="$LIBS $bzlib_lib_libs"

        dnl
        dnl Check bzip2 headers
        dnl
        AC_MSG_CHECKING([for bzip2 headers in $bzlib_include_dir])

        AC_LANG_PUSH([C++])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM(
                [[
@%:@include <bzlib.h>
                ]],
                [[]]
            )],
            [
            BZIP2_CFLAGS="-I$bzlib_include_dir"
            bzlib_header_found="yes"
            AC_MSG_RESULT([found])
            ],
            [
            bzlib_header_found="no"
            AC_MSG_RESULT([not found])
            ]
        )
        AC_LANG_POP([C++])

        dnl
        dnl Check bzip2 libraries
        dnl
        if test "$bzlib_header_found" = "yes"; then

            AC_MSG_CHECKING([for bzip2 library])

            AC_LANG_PUSH([C++])
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM(
                    [[
@%:@include <bzlib.h>
                    ]],
                    [[
    const char *version;
    
    version = BZ2_bzlibVersion();
                    ]]
                )],
                [
                BZIP2_LDFLAGS="$bzlib_lib_flags"
                BZIP2_LIBS="$bzlib_lib_libs"
                bzlib_lib_found="yes"
                AC_MSG_RESULT([found])
                ],
                [
                bzlib_lib_found="no"
                AC_MSG_RESULT([not found])
                ]
            )
            AC_LANG_POP([C++])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
        LIBS="$saved_LIBS"
    fi

    AC_MSG_CHECKING([for bzip2 compression library])

    if test "$run_bzlib_test" = "yes"; then
        if test "$bzlib_header_found" = "yes" -a "$bzlib_lib_found" = "yes"; then
            AC_SUBST([BZIP2_CFLAGS])
            AC_SUBST([BZIP2_LDFLAGS])
            AC_SUBST([BZIP2_LIBS])
            AC_SUBST([HAVE_BZIP2])

            AC_DEFINE([HAVE_BZIP2], [1],
                [Define to 1 if bzip2 library is available])

            HAVE_BZIP2="yes"
        else
            HAVE_BZIP2="no"
        fi

        AC_MSG_RESULT([$HAVE_BZIP2])


    else
        HAVE_BZIP2="no"
        AC_MSG_RESULT([$HAVE_BZIP2])

        if test "$bzlib_requested" = "yes"; then
            AC_MSG_WARN([bzip2 compression support requested but headers or library not found. Specify valid prefix of bzip2 using --with-bzip2=@<:@DIR@:>@ or provide include directory and linker flags using --with-bzip2-inc and --with-bzip2-lib])
        fi
    fi
])

