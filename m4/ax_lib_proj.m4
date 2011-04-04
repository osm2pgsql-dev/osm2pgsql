# SYNOPSIS
#
#   AX_LIB_PROJ()
#
# DESCRIPTION
#
#   This macro provides tests of availability of the proj
#   projection library. This macro checks for proj
#   headers and libraries and defines compilation flags
#
#   Macro supports following options and their values:
#
#   1) Single-option usage:
#
#     --with-proj      -- yes, no, or path to proj library 
#                          installation prefix
#
#   2) Three-options usage (all options are required):
#
#     --with-proj=yes
#     --with-proj-inc  -- path to base directory with proj headers
#     --with-proj-lib  -- linker flags for proj
#
#   This macro calls:
#
#     AC_SUBST(PROJ_CFLAGS)
#     AC_SUBST(PROJ_LDFLAGS)
#
#   And sets:
#
#     HAVE_PROJ
#
# LICENSE
#
#   Copyright (c) 2009 Hartmut Holzgraefe <hartmut@php.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_LIB_PROJ],
[
    AC_ARG_WITH([proj],
        AC_HELP_STRING([--with-proj=@<:@ARG@:>@],
            [use proj library from given prefix (ARG=path); check standard prefixes (ARG=yes); disable (ARG=no)]
        ),
        [
        if test "$withval" = "yes"; then
            if test -f /usr/local/include/proj_api.h ; then
                proj_prefix=/usr/local
            elif test -f /usr/include/proj_api.h ; then
                proj_prefix=/usr
            else
                proj_prefix=""
            fi
            proj_requested="yes"
        elif test -d "$withval"; then
            proj_prefix="$withval"
            proj_requested="yes"
        else
            proj_prefix=""
            proj_requested="no"
        fi
        ],
        [
        dnl Default behavior is implicit yes
        if test -f /usr/local/include/proj_api.h ; then
            proj_prefix=/usr/local
        elif test -f /usr/include/proj_api.h ; then
            proj_prefix=/usr
        else
            proj_prefix=""
        fi
        ]
    )

    AC_ARG_WITH([proj-inc],
        AC_HELP_STRING([--with-proj-inc=@<:@DIR@:>@],
            [path to proj library headers]
        ),
        [proj_include_dir="$withval"],
        [proj_include_dir=""]
    )
    AC_ARG_WITH([proj-lib],
        AC_HELP_STRING([--with-proj-lib=@<:@ARG@:>@],
            [link options for proj library]
        ),
        [proj_lib_flags="$withval"],
        [proj_lib_flags=""]
    )

    PROJ_CFLAGS=""
    PROJ_LDFLAGS=""

    dnl
    dnl Collect include/lib paths and flags
    dnl
    run_proj_test="no"

    if test -n "$proj_prefix"; then
        proj_include_dir="$proj_prefix/include"
        proj_lib_flags="-L$proj_prefix/lib -lproj"
        run_proj_test="yes"
    elif test "$proj_requested" = "yes"; then
        if test -n "$proj_include_dir" -a -n "$proj_lib_flags"; then
            run_proj_test="yes"
        fi
    else
        run_proj_test="no"
    fi

    dnl
    dnl Check proj files
    dnl
    if test "$run_proj_test" = "yes"; then

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$proj_include_dir"

        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $proj_lib_flags"

        dnl
        dnl Check proj headers
        dnl
        AC_MSG_CHECKING([for proj headers in $proj_include_dir])

        AC_LANG_PUSH([C++])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM(
                [[
@%:@include <proj_api.h>
                ]],
                [[]]
            )],
            [
            PROJ_CFLAGS="-I$proj_include_dir"
            proj_header_found="yes"
            AC_MSG_RESULT([found])
            ],
            [
            proj_header_found="no"
            AC_MSG_RESULT([not found])
            ]
        )
        AC_LANG_POP([C++])

        dnl
        dnl Check proj libraries
        dnl
        if test "$proj_header_found" = "yes"; then

            AC_MSG_CHECKING([for proj library])

            AC_LANG_PUSH([C++])
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM(
                    [[
@%:@include <proj_api.h>
                    ]],
                    [[
    /* TODO add a real test */
                    ]]
                )],
                [
                PROJ_LDFLAGS="$proj_lib_flags"
                proj_lib_found="yes"
                AC_MSG_RESULT([found])
                ],
                [
                proj_lib_found="no"
                AC_MSG_RESULT([not found])
                ]
            )
            AC_LANG_POP([C++])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
    fi

    AC_MSG_CHECKING([for proj projection library])

    if test "$run_proj_test" = "yes"; then
        if test "$proj_header_found" = "yes" -a "$proj_lib_found" = "yes"; then

            AC_SUBST([PROJ_CFLAGS])
            AC_SUBST([PROJ_LDFLAGS])

            HAVE_PROJ="yes"
        else
            HAVE_PROJ="no"
        fi

        AC_MSG_RESULT([$HAVE_PROJ])


    else
        HAVE_PROJ="no"
        AC_MSG_RESULT([$HAVE_PROJ])

        if test "$proj_requested" = "yes"; then
            AC_MSG_WARN([proj projection support requested but headers or library not found. Specify valid prefix of proj using --with-proj=@<:@DIR@:>@ or provide include directory and linker flags using --with-proj-inc and --with-proj-lib])
        fi
    fi
])

