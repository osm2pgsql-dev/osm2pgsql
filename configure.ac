dnl Process this file with autoconf to produce a configure script.
AC_INIT(osm2pgsql, 0.85.0)

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

################################################################
## See if we are running on mingw
# http://osdir.com/ml/gnu.mingw.devel/2003-09/msg00040.html
# Note: Windows 95 WINVER=0x400
# Windows 98 WINVER=0x400  _WIN32_WINDOWS=0x0410
# Windows Me WINVER=0x400  _WIN32_WINDOWS=0x0490
# Windows NT 4.0 WINVER=0x0400  _WIN32_WINNT=0x0400
# Windows NT 4.0 SP3 WINVER=0x0400 _WIN32_WINNT=0x0403
# Windows 2000 WINVER=0x500 _WIN32_WINNT=0x0500
# Windows XP WINVER=0x501 _WIN32_WINNT=0x0501
# Windows Server 2003 WINVER=0x502 _WIN32_WINNT=0x0502
#
# mingw32 includes  i686-w64-mingw32 and  x86_64-w64-mingw32

mingw="no"
case $host in
  *-*-*linux*-*) 
     AC_DEFINE([__LINUX__],1,[Linux operating system functions])
     ;;

  *-*-mingw32*)   
     LIBS="$LIBS -lpsapi -lws2_32 -lgdi32 -lstdc++"  
     CPPFLAGS="-DUNICODE -D_UNICODE -D__MSVCRT_VERSION__=0x0601 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -g $CPPFLAGS"
     CPPFLAGS="$CPPFLAGS --static "
     CFLAGS="$CFLAGS --static -static-libgcc -static-libstdc++"
     CXXFLAGS="$CXXFLAGS -Wno-format "  # compiler mingw-4.3.0 is broken on I64u formats
     CXXFLAGS="$CXXFLAGS --static -static-libgcc -static-libstdc++"
     LDFLAGS="$LDFLAGS --static"
     mingw="yes"
     ;;		 		     
esac

if test x"${mingw}" == "xno" ; then
  # Bring additional directories where things might be found into our
  # search path. I don't know why autoconf doesn't do this by default
  for spfx in /usr/local /opt/local /sw /usr/local/ssl; do
    AC_MSG_NOTICE([checking ${spfx}/include])
    if test -d ${spfx}/include; then
        CPPFLAGS="-isystem${spfx}/include $CPPFLAGS"
        LDFLAGS="-L${spfx}/lib $LDFLAGS"
        AC_MSG_NOTICE([ *** ADDING ${spfx}/include to CPPFLAGS *** ])
        AC_MSG_NOTICE([ *** ADDING ${spfx}/lib to LDFLAGS *** ])
    fi
  done
  AC_MSG_NOTICE([ CPPFLAGS = ${CPPFLAGS} ])        
  AC_MSG_NOTICE([ LDFLAGS = ${LDFLAGS} ])        
fi

if [ test -r /bin/uname.exe ] || [ test -r /mingw/bin/uname.exe ]; then
  if test `uname -o` == "Msys" ; then
    AC_MSG_NOTICE([Compiling with Msys. Setting flags appropriately.])
    LIBS="$LIBS -lws2_32 -lgdi32"
    LDFLAGS="$LDFLAGS -Wl,--enable-auto-import"
  fi
fi

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

dnl AX_CFLAGS_WARN_ALL
dnl AX_CXXFLAGS_WARN_ALL

dnl Make sure we have libtool installed
AM_PROG_LIBTOOL

LFS_CFLAGS=`getconf LFS_CFLAGS`
AC_SUBST(LFS_CFLAGS)

AC_CHECK_FUNC(lseek64,[AC_DEFINE(HAVE_LSEEK64, [1], [lseek64 is present])],[AX_COMPILE_CHECK_SIZEOF(off_t)])
AC_CHECK_FUNCS([posix_fallocate posix_fadvise sync_file_range fork])


dnl legacy 32bit ID mode
AC_ARG_ENABLE([64bit-ids],
    AS_HELP_STRING([--disable-64bit-ids], [Disable 64bit IDs for OSM IDs]), 
    [ if test "$enableval" = "yes"
      then
        AC_DEFINE(OSMID64, [1], [Enable 64bit OSM IDs])
      fi
    ], [ AC_DEFINE(OSMID64, [1], [Enable 64bit OSM IDs])])


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
if test "x$GEOS_VERSION" = "x" 
then
  AC_MSG_ERROR([geos library not found]);
fi

dnl Check for Proj library
AX_LIB_PROJ
if test "$HAVE_PROJ" = "no" 
then
  AC_MSG_ERROR([required library not found]);
fi

dnl Check for protobuf-c library and protoc-c binary
if test x"${mingw}" == "xyes" ; then
  if test "${with_protobuf_c+set}" = set; then :
  withval=$with_protobuf_c;
    if test "$withval" = "yes"; then
      protobuf_c_include_dir="${prefix}/include"
      protobuf_c_lib_libs="-L${prefix}/lib -lprotobuf-c"
    fi
  fi
fi

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
#################################################################
## PTHREAD support required by bloom.h
## With special nods to compiling under mingw
#
#if test  x"$mingw" = x"yes";  then
#  #AC_DEFINE([HAVE_STRUCT_TIMESPEC],1,[Required for mingw])
#  CFLAGS="$CFLAGS -mthreads "
#  CPPFLAGS="-DPTW32_STATIC_LIB $CPPFLAGS"
#  CXXFLAGS="$CXXFLAGS -mthreads "
#  AC_DEFINE(HAVE_PTHREAD,1,[Defined to POSIX threads for mingw])
#else
#  m4_include([m4/ax_pthread.m4])
#  AC_MSG_CHECKING([Running pthread test from http://www.gnu.org/software/autoconf-archive/ax_pthread.html..])
#  AX_PTHREAD([
#              echo Using settings from AX_PTHREAD
#              LIBS="$LIBS $PTHREAD_LIBS"
#              CFLAGS="  $PTHREAD_CFLAGS $CFLAGS"
#              CXXFLAGS="$PTHREAD_CFLAGS $CXXFLAGS "
#              CC="$PTHREAD_CC"],
#            [AC_MSG_ERROR(hashdb requires pthreads)])
#fi
#
#AC_CHECK_HEADERS([pthread.h])
#AC_CHECK_LIB([pthreadGC2],[pthread_create])

################################################################
# PTHREAD support
# With special nods to compiling under mingw

if test  x"$mingw" = x"yes";  then
  #AC_DEFINE([HAVE_STRUCT_TIMESPEC],1,[Required for mingw])
  CFLAGS="$CFLAGS -mthreads "
  CPPFLAGS="-DPTW32_STATIC_LIB $CPPFLAGS"
  CXXFLAGS="$CXXFLAGS -mthreads "
  AC_DEFINE(HAVE_PTHREAD,1,[Defined to POSIX threads for mingw])
else
  m4_include([m4/ax_pthread.m4])
  AC_MSG_CHECKING([Running pthread test from http://www.gnu.org/software/autoconf-archive/ax_pthread.html..])
  AX_PTHREAD([
              echo Using settings from AX_PTHREAD
              LIBS="$PTHREAD_LIBS $LIBS"
              CFLAGS="  $PTHREAD_CFLAGS $CFLAGS"
              CXXFLAGS="$PTHREAD_CFLAGS $CXXFLAGS "
              CC="$PTHREAD_CC"],
            [AC_MSG_ERROR(bulk_extractor requires pthreads)])
fi

AC_CHECK_HEADERS([pthread.h])
AC_CHECK_LIB([pthreadGC2],[pthread_create])

################################################################
##
## Multithreading stuff

# On mingw, be sure to use the static version and be sure we are using mthread option
# (which should be a no-op on later version of G++ anyway)

AC_CHECK_FUNCS([pthread_win32_process_attach_np pthread_win32_process_detach_np pthread_win32_thread_attach_np pthread_win32_thread_detach_np ])

AC_MSG_CHECKING([for __sync_fetch_and_add])
AC_LINK_IFELSE(
   [AC_LANG_SOURCE([
    int main(void) { int i; return __sync_fetch_and_add(&i, 0); }
   ])],
   [
     AC_MSG_RESULT([yes])
     AC_DEFINE(HAVE___SYNC_FETCH_AND_ADD,1,[Defined if __sync_fetch_and_add was detected])
    ],
AX_PTHREAD(,[AC_MSG_ERROR([no])])
)


# end PTHREAD SUPPORT
################################################################


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

