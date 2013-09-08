#!/bin/bash
#
#  Author:       Jason Huntley
#  Email:        onepremise@gmail.com
#  Description:  Cygwin Package script
#
#  Change Log
#
#  Date                   Description                 Initials
#-------------------------------------------------------------
#  04-11-13             Initial Coding                  JAH
#=============================================================

if [ ! -e "cygwin-package" ]; then
  mkdir cygwin-package
fi

echo
echo Copying Executable...
echo

cp -rfv default.style cygwin-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv 900913.sql cygwin-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv README cygwin-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }
cp -rfv .libs/osm2pgsql.exe cygwin-package || { stat=$?; echo "Packaging failed, aborting" >&2; exit $stat; }

echo
echo Copying Dependent Libraries...
echo

cp -rfv /bin/cygcrypt*.dll cygwin-package
cp -rfv /bin/cyggcc*.dll cygwin-package
cp -rfv /usr/local/bin/cyggeos*.dll cygwin-package
cp -rfv /bin/cygiconv*.dll cygwin-package
cp -rfv /bin/cygintl*.dll cygwin-package
cp -rfv /bin/cyglber*.dll cygwin-package
cp -rfv /bin/cygldap*.dll cygwin-package
cp -rfv /bin/cyglzma*.dll cygwin-package
cp -rfv /bin/cygpq*.dll cygwin-package
cp -rfv /usr/local/bin/cygproj*.dll cygwin-package
cp -rfv /usr/local/bin/cygproto*.dll cygwin-package
cp -rfv /bin/cygsasl*.dll cygwin-package
cp -rfv /bin/cygssl*.dll cygwin-package
cp -rfv /bin/cygstdc++**.dll cygwin-package
cp -rfv /bin/cygwin*.dll cygwin-package
cp -rfv /bin/cygxml2*.dll cygwin-package
cp -rfv /bin/cygz*.dll cygwin-package

echo
echo Creating Archive...
echo

zip -r9 cygwin-package.zip cygwin-package

echo
echo Packaging Complete.
echo

exit 0
