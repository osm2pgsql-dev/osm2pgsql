dnl Process this file with autoconf to produce a configure script.
AC_INIT([osm2pgsql],[0.89.0-dev])

dnl Required autoconf version
AC_PREREQ([2.61])

AX_CONFIG_NICE

dnl Allow maintainer mode to be disabled (debian build scripts do this)
AM_MAINTAINER_MODE([enable])

dnl use automake to generate standard Makefiles
AM_INIT_AUTOMAKE([1.11 foreign dist-bzip2 std-options])
AM_SILENT_RULES([yes])

dnl Additional macro definitions are in here
AC_CONFIG_MACRO_DIR([m4])

dnl Generate configuration header file
AC_CONFIG_HEADER(config.h)

AC_USE_SYSTEM_EXTENSIONS
AC_HEADER_SYS_WAIT
AC_FUNC_MMAP

dnl Find C++ compiler
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11([noext],[mandatory])

AX_CFLAGS_WARN_ALL
AX_CXXFLAGS_WARN_ALL

dnl Make sure we have libtool installed
LT_INIT

LFS_CFLAGS=`getconf LFS_CFLAGS`
AC_SUBST(LFS_CFLAGS)

AC_CHECK_FUNC(lseek64,[AC_DEFINE(HAVE_LSEEK64, [1], [lseek64 is present])],[AX_COMPILE_CHECK_SIZEOF(off_t)])
AC_CHECK_FUNCS([posix_fallocate posix_fadvise sync_file_range fork])

dnl Check for libexpat library
AX_LIB_EXPAT
if test "$HAVE_EXPAT" = "no"
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

dnl Check for protobuf library and protoc binary
PKG_CHECK_MODULES(PROTOBUF, protobuf-lite >= 2.4.0,
   [ AC_CHECK_PROG([PROTOC], [protoc], [protoc])
     AS_IF([test "x${PROTOC}" == "x"],
           [AC_MSG_ERROR([protoc compiler not found.])])
   ], [AC_MSG_ERROR([protobuf development libraries not found.])]
)

dnl Check for PostgresSQL client library
AX_LIB_POSTGRESQL
if test "x$POSTGRESQL_VERSION" = "x"
then
    AC_MSG_ERROR([postgresql client library not found])
fi


dnl Check for pthread library
AX_PTHREAD(,[AC_MSG_ERROR([no])])

dnl Check for Boost libraries
dnl Boost json parser in 1.49 has a bug when compiled with C++11
dnl see https://svn.boost.org/trac/boost/ticket/6785
dnl As we require C++11 below, 1.50 is mandatory here.
AX_BOOST_BASE([1.50], , [AC_MSG_ERROR([cannot find Boost libraries, which are are required for building osm2pgsql. Please install libboost-dev.])])

AX_BOOST_SYSTEM
AX_BOOST_FILESYSTEM
AX_BOOST_THREAD
if test "x$BOOST_SYSTEM_LIB" = "x" -o "x$BOOST_FILESYSTEM_LIB" = "x" -o "x$BOOST_THREAD_LIB" = "x"
then
    AC_MSG_ERROR([One or more of the mandatory Boost libraries not found.])
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

dnl Enable fixed point
AC_ARG_WITH([fixed-point],
  [AS_HELP_STRING([--without-fixed-point],
    [use double instead of fixed point floats for coordinates])],
    [],
    [AC_DEFINE([FIXED_POINT], [1], [Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision])])

dnl Generate Makefile
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

if test "$HAVE_LUA" != "yes"
then
    AC_MSG_WARN([
lua libraries not found. You will NOT be able to use lua scripts for tag transform.

To enable lua support, the lua interpreter and libraries are required. 
Look for packages named: lua5.2 liblua5.2-dev
])
fi

