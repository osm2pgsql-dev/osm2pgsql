#!/bin/bash
#
#  Author:       Jason Huntley
#  Email:        onepremise@gmail.com
#  Description:  mingw Package script
#
#  Change Log
#
#  Date                   Description                 Initials
#-------------------------------------------------------------
#  01-08-14             Initial Coding                  JAH
#=============================================================

if [ ! -e "mingw-package" ]; then
  mkdir mingw-package
fi

echo
echo Copying Executable...
echo

cp -rfv default.style mingw-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv 900913.sql mingw-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv README mingw-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv .libs/osm2pgsql.exe mingw-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }

echo
echo Copying Dependent Libraries...
echo

_binpath=/bin

if [ -e /mingw/bin ]; then
    _binpath=/mingw/bin
fi

cp -rfv $_binpath/libgcc_s_sjlj-*.dll mingw-package
cp -rfv $_binpath/libgeos*.dll mingw-package
cp -rfv $_binpath/libiconv-*.dll mingw-package
cp -rfv $_binpath/libicudata5*.dll mingw-package
cp -rfv $_binpath/libicuuc5*.dll mingw-package
cp -rfv $_binpath/libpq*.dll mingw-package
cp -rfv $_binpath/libproj-*.dll mingw-package
cp -rfv $_binpath/libstdc++-*.dll mingw-package
cp -rfv $_binpath/libxml2-*.dll mingw-package
cp -rfv $_binpath/zlib*.dll mingw-package

echo
echo Creating Archive...
echo

zip -r9 mingw-package.zip mingw-package

echo
echo Packaging Complete.
echo

exit 0
