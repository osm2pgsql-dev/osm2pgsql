dnl Process this file with autoconf to produce a configure script.
AC_INIT(osm2pgsql, 0.80.0)

dnl Required autoconf version
AC_PREREQ(2.61)

AX_CONFIG_NICE

dnl use automake to generate standard Makefiles
AM_INIT_AUTOMAKE([1.9.6 dist-bzip2 std-options check-news])

dnl use 'silent' make rules by default (disabled for now)
dnl AM_INIT_AUTOMAKE([1.11 dist-bzip2 silent-rules])
dnl AM_SILENT_RULES([yes])

dnl Additional macro definitions are in here
AC_CONFIG_MACRO_DIR([m4])

dnl Generate configuration header file
AC_CONFIG_HEADER(config.h)

AC_HEADER_SYS_WAIT

dnl Find C compiler
AC_PROG_CC_C99

dnl Find C++ compiler
AC_PROG_CXX

dnl Make sure we have libtool installed
AM_PROG_LIBTOOL

dnl Check for libxml2 library
AX_LIB_XML2
if test "$HAVE_XML2" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl check for zlib library
AX_LIB_ZLIB
if test "$HAVE_ZLIB" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl Check for bzip2 library
AX_LIB_BZIP2
if test "$HAVE_BZIP2" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl Check for Geos library
AX_LIB_GEOS
if test "$HAVE_GEOS" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl Check for Proj library
AX_LIB_PROJ
if test "$HAVE_PROJ" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl Check for protobuf-c library and protoc-c binary
AX_LIB_PROTOBUF_C([0.14])

dnl Decide whether to include PBF import support
BUILD_READER_PBF=no
if test "$HAVE_PROTOBUF_C" = "yes"
then
  if test "$PROTOC_C" != "false" -o -f $srcdir/fileformat.pb-c.c
  then
    BUILD_READER_PBF=yes
    AC_DEFINE([BUILD_READER_PBF], [1], [Requirements for building the PBF reader are met])
  fi
fi
AM_CONDITIONAL([READER_PBF], [test "$BUILD_READER_PBF" = "yes"])

dnl Check for PostgresSQL client library
AX_LIB_POSTGRESQL
if test -f $POSTGRESQL_PGXS
then
  GAZETTEER_SUBDIR=gazetteer
fi
AC_SUBST([GAZETTEER_SUBDIR])

dnl Check for pthread library
AX_PTHREAD(,[AC_MSG_ERROR([no])])


dnl Generate Makefile
AC_OUTPUT(Makefile legacy/Makefile gazetteer/Makefile)

