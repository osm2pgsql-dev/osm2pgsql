dnl Process this file with autoconf to produce a configure script.
AC_INIT(osm2pgsql, 0.87.4-dev)

dnl Required autoconf version
AC_PREREQ(2.61)

AX_CONFIG_NICE

dnl Allow maintainer mode to be disabled (debian build scripts do this)
AM_MAINTAINER_MODE([enable])

dnl use automake to generate standard Makefiles
AM_INIT_AUTOMAKE([1.9.6 dist-bzip2 std-options check-news])

dnl use 'silent' make rules by default (disabled for now)
dnl AM_INIT_AUTOMAKE([1.11 dist-bzip2 silent-rules])
dnl AM_SILENT_RULES([yes])

dnl Additional macro definitions are in here
AC_CONFIG_MACRO_DIR([m4])

dnl Generate configuration header file
AC_CONFIG_HEADER(config.h)

AC_GNU_SOURCE
AC_HEADER_SYS_WAIT
AC_FUNC_MMAP

dnl Find C compiler
dnl AC_PROG_CC_C99

dnl Find C++ compiler
AC_PROG_CXX
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)
if test "$HAVE_CXX" = "no" 
then
  AC_MSG_ERROR([Could not find a c++ compiler]);
fi

AX_CFLAGS_WARN_ALL
AX_CXXFLAGS_WARN_ALL

dnl Make sure we have libtool installed
AM_PROG_LIBTOOL

LFS_CFLAGS=`getconf LFS_CFLAGS`
AC_SUBST(LFS_CFLAGS)

AC_CHECK_FUNC(lseek64,[AC_DEFINE(HAVE_LSEEK64, [1], [lseek64 is present])],[AX_COMPILE_CHECK_SIZEOF(off_t)])
AC_CHECK_FUNCS([posix_fallocate posix_fadvise sync_file_range fork])

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
AX_LIB_GEOS([3.1])

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
  if test "$PROTOC_C" != "false"
  then
    BUILD_READER_PBF=yes
    AC_DEFINE([BUILD_READER_PBF], [1], [Requirements for building the PBF reader are met])
  fi
fi
AM_CONDITIONAL([READER_PBF], [test "$BUILD_READER_PBF" = "yes"])

dnl Check for PostgresSQL client library
AX_LIB_POSTGRESQL
if test "x$POSTGRESQL_VERSION" = "x"
then
    AC_MSG_ERROR([postgresql client library not found])
fi


dnl Check for pthread library
AX_PTHREAD(,[AC_MSG_ERROR([no])])

dnl Check for Boost libraries
AX_BOOST_BASE([1.48], , [AC_MSG_ERROR([cannot find Boost libraries, which are are required for building osm2pgsql. Please install libboost-dev.])])

AX_BOOST_SYSTEM
AX_BOOST_FILESYSTEM
AX_BOOST_THREAD
if test "x$BOOST_SYSTEM_LIB" = "x" -o "x$BOOST_FILESYSTEM_LIB" = "x" -o "x$BOOST_THREAD_LIB" = "x"
then
    AC_MSG_ERROR([One or more of the mandatory Boost libraries not found.])
fi

dnl Check if Boost is recent enough for lockfree, and if it hasn't been overridden
AC_ARG_WITH([lockfree],
  [AS_HELP_STRING([--without-lockfree],
    [disable lockfree queue])],
    [],
    [with_lockfree=yes])

if test "x$with_lockfree" = "xyes"
then
  AX_BOOST_BASE([1.53],
                [
                  AX_BOOST_ATOMIC
                  if test "x$BOOST_ATOMIC_LIB" != "x"; then
                    AC_DEFINE([HAVE_LOCKFREE], [1], [Using lockfree queue])
                  fi
                ]
                , [])
fi

dnl Boost json parser in 1.49 has a bug when compiled with C++11
dnl see https://svn.boost.org/trac/boost/ticket/6785
AC_ARG_WITH([cxx11],
  [AS_HELP_STRING([--without-cxx11],
    [do not check for C++11-capable compiler (for testing only)])],
    [],
    [with_cxx11=yes])

if test "x$with_cxx11" = "xyes"; then
    AX_BOOST_BASE([1.50], [ AX_CXX_COMPILE_STDCXX_11(,optional) ], [])
fi

dnl Check for Lua libraries and headers
AX_PROG_LUA([5.0],[],[
    AX_LUA_HEADERS([
        AX_LUA_LIBS([
            AC_DEFINE([HAVE_LUA], [1], [Requirements for lua are met])
            HAVE_LUA=yes
        ],[AC_MSG_WARN([cannot find Lua libs])])
    ],[AC_MSG_WARN([cannot find Lua includes])])
],[AC_MSG_WARN([cannot find Lua interpreter])])

dnl Generate Makefile
AC_OUTPUT(Makefile)

if test "$BUILD_READER_PBF" != "yes"
then
    AC_MSG_WARN([
protobuf libraries not found. You will NOT be able to import PBF files.

To enable PBF support, the protobuf library and compiler are required. 
Look for packages named: libprotobuf-c0-dev protobuf-c-compiler
])
fi

if test "$HAVE_LUA" != "yes"
then
    AC_MSG_WARN([
lua libraries not found. You will NOT be able to use lua scripts for tag transform.

To enable lua support, the lua interpreter and libraries are required. 
Look for packages named: lua5.2 liblua5.2-dev
])
fi

