# SYNOPSIS
#
#   AX_LIB_PROTOBUF_C()
#
# DESCRIPTION
#
#   This macro provides tests of availability of the Google
#   Protocol Buffers C library. This macro checks for protobufr-c
#   headers and libraries and defines compilation flags
#
#   Macro supports following options and their values:
#
#   1) Single-option usage:
#
#     --with-protobuf_c      -- yes, no, or path to protobuf_c library 
#                          installation prefix
#
#   2) Three-options usage (all options are required):
#
#     --with-protobuf_c=yes
#     --with-protobuf_c-inc  -- path to base directory with protobuf_c headers
#     --with-protobuf_c-lib  -- linker flags for protobuf_c
#
#   This macro calls:
#
#     AC_SUBST(PROTOBUF_C_CFLAGS)
#     AC_SUBST(PROTOBUF_C_LDFLAGS)
#     AC_SUBST(PROTOBUF_C_LIBS)
#
#   And sets:
#
#     HAVE_PROTOBUF_C
#
# LICENSE
#
#   Copyright (c) 2009 Hartmut Holzgraefe <hartmut@php.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_LIB_PROTOBUF_C],
[
    protobuf_c_wanted_version=$1

    AC_MSG_CHECKING([for protobuf-c $protobuf_c_wanted_version])
    AC_MSG_RESULT

    AC_ARG_WITH([protobuf-c],
        AC_HELP_STRING([--with-protobuf-c=@<:@ARG@:>@],
            [use protobuf-c library from given prefix (ARG=path); check standard prefixes (ARG=yes); disable (ARG=no)]
        ),
        [
        if test "$withval" = "yes"; then
            if test -f /usr/local/include/google/protobuf-c/protobuf-c.h ; then
                protobuf_c_prefix=/usr/local
            elif test -f /usr/include/google/protobuf-c/protobuf-c.h ; then
                protobuf_c_prefix=/usr
            fi
            protobuf_c_requested="yes"
        elif test -d "$withval"; then
            protobuf_c_prefix="$withval"
            protobuf_c_requested="yes"
        else
            protobuf_c_prefix=""
            protobuf_c_requested="no"
        fi
        ],
        [
        dnl Default behavior is implicit yes
        if test -f /usr/local/include/google/protobuf-c/protobuf-c.h ; then
            protobuf_c_prefix=/usr/local
        elif test -f /usr/include/google/protobuf-c/protobuf-c.h ; then
            protobuf_c_prefix=/usr
        else
            protobuf_c_prefix=""
        fi
        ]
    )

    AC_ARG_WITH([protobuf-c-inc],
        AC_HELP_STRING([--with-protobuf-c-inc=@<:@DIR@:>@],
            [path to protobuf-c library headers]
        ),
        [protobuf_c_include_dir="$withval"]
    )
    AC_ARG_WITH([protobuf-c-lib],
        AC_HELP_STRING([--with-protobuf-c-lib=@<:@ARG@:>@],
            [link options for protobuf-c library]
        ),
        [protobuf_c_lib_libs="$withval"]
    )

    PROTOBUF_C_CFLAGS=""
    PROTOBUF_C_LDFLAGS=""
    PROTOBUF_C_LIBS=""

    dnl
    dnl Collect include/lib paths and flags
    dnl
    run_protobuf_c_test="no"

    if test -n "$protobuf_c_prefix"; then
        protobuf_c_include_dir="$protobuf_c_prefix/include"
        protobuf_c_lib_flags="-L$protobuf_c_prefix/lib"
        protobuf_c_lib_libs="-lprotobuf-c"
        run_protobuf_c_test="yes"
    elif test "$protobuf_c_requested" = "yes"; then
        if test -n "$protobuf_c_include_dir" -a -n "$protobuf_c_lib_libs"; then
            run_protobuf_c_test="yes"
        fi
    else
        run_protobuf_c_test="no"
    fi

    dnl
    dnl Check protobuf_c files
    dnl
    if test "$run_protobuf_c_test" = "yes"; then

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$protobuf_c_include_dir"

        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $protobuf_c_lib_flags"

        saved_LIBS="$LIBS"
        LIBS="$LIBS $protobuf_c_lib_libs"

        dnl
        dnl Check protobuf_c headers
        dnl
        AC_MSG_CHECKING([for protobuf_c headers in $protobuf_c_include_dir])

        AC_LANG_PUSH([C++])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM(
                [[
@%:@include <google/protobuf-c/protobuf-c.h>
                ]],
                [[]]
            )],
            [
            PROTOBUF_C_CFLAGS="-I$protobuf_c_include_dir"
            protobuf_c_header_found="yes"
            AC_MSG_RESULT([found])
            ],
            [
            protobuf_c_header_found="no"
            AC_MSG_RESULT([not found])
            ]
        )
        AC_LANG_POP([C++])

        dnl
        dnl Check protobuf_c libraries
        dnl
        if test "$protobuf_c_header_found" = "yes"; then

            AC_MSG_CHECKING([for protobuf_c library])

            AC_LANG_PUSH([C++])
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM(
                    [[
@%:@include <google/protobuf-c/protobuf-c.h>
                    ]],
                    [[
    protobuf_c_service_destroy((ProtobufCService *)NULL);
                    ]]
                )],
                [
                PROTOBUF_C_LDFLAGS="$protobuf_c_lib_flags"
                PROTOBUF_C_LIBS="$protobuf_c_lib_libs"
                protobuf_c_lib_found="yes"
                AC_MSG_RESULT([found])
                ],
                [
                protobuf_c_lib_found="no"
                AC_MSG_RESULT([not found])
                ]
            )
            AC_LANG_POP([C++])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
        LIBS="$saved_LIBS"
    fi

    protobuf_c_version_ok=yes
    if test "x$protobuf_c_wanted_version" != "x"
    then
      AC_MSG_CHECKING([for protobuf-c version >= $protobuf_c_wanted_version])
      AC_MSG_RESULT

      dnl protobuf-c does not provide any version information in its header
      dnl files or from within the library itself, so we have to check
      dnl for availability of features here for now ...

      dnl protobuf-c 0.14 introduced member 'packed' in ProtobufCFieldDescriptor
      saved_CFLAGS=$CFLAGS
      CFLAGS="$CFLAGS $PROTOBUF_C_CFLAGS"
      AX_COMPARE_VERSION([$protobuf_c_wanted_version], [ge], [0.14],
         [AC_CHECK_MEMBER([ProtobufCFieldDescriptor.packed],,
                          [AC_CHECK_MEMBER([ProtobufCFieldDescriptor.flags],,
                                           [protobuf_c_version_ok="no"],
                                           [[#include <protobuf-c/protobuf-c.h>]])],
                          [[#include <google/protobuf-c/protobuf-c.h>]
         ])
      ])
      CFLAGS=$saved_CFLAGS
      
      AC_MSG_RESULT([protobuf-c >= $protobuf_c_wanted_version: $protobuf_c_version_ok])
    fi


    AC_MSG_CHECKING([for protobuf-c usability])

    if test "$run_protobuf_c_test" = "yes"; then
        if test "$protobuf_c_header_found" = "yes" -a "$protobuf_c_lib_found" = "yes" -a "$protobuf_c_version_ok" = "yes"
        then
            AC_SUBST([PROTOBUF_C_CFLAGS])
            AC_SUBST([PROTOBUF_C_LDFLAGS])
            AC_SUBST([PROTOBUF_C_LIBS])
            AC_SUBST([HAVE_PROTOBUF_C])

            AC_DEFINE([HAVE_PROTOBUF_C], [1],
                [Define to 1 if protobuf_c library is available])

            HAVE_PROTOBUF_C="yes"
	    AC_MSG_RESULT([yes])

	    protoc_path="$protobuf_c_prefix/bin:$PATH"
	    AC_PATH_PROG(PROTOC_C, protoc-c, false, $protoc_path)
        else
            HAVE_PROTOBUF_C="no"
            AC_MSG_RESULT([no])
        fi


    else
        HAVE_PROTOBUF_C="no"
        AC_MSG_RESULT([no])

        if test "$protobuf_c_requested" = "yes"; then
            AC_MSG_WARN([protobuf-c support requested but headers or library not found. Specify valid prefix of protobuf-c using --with-protobuf-c=@<:@DIR@:>@ or provide include directory and linker flags using --with-protobuf-c-inc and --with-protobuf-c-lib])
        fi
    fi
])



