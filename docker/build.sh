#!/usr/bin/env bash
set -Eeuo pipefail

OSM2PGSQL_CMPARAMS="  -DBUILD_TESTS=ON "
OSM2PGSQL_CMPARAMS+=" -DCMAKE_BUILD_TYPE=Debug "
OSM2PGSQL_CMPARAMS+=" -DWITH_LUA=${LUA_OPTION:-ON} "
OSM2PGSQL_CMPARAMS+=" -DWITH_LUAJIT=${LUAJIT_OPTION:-ON} "


echo "---------------------"
echo "GCCVER           : $GCCVER   [ $GCC_VERSION ] "
echo "cmake params     : $OSM2PGSQL_CMPARAMS "
echo "CXXFLAGS         : $CXXFLAGS"
echo "LUA_OPTION       : $LUA_OPTION"
echo "LUAJIT_OPTION    : $LUAJIT_OPTION"
echo "RUNTEST (filter) : $RUNTEST"
echo "----------------------"

# Hard clean "build" directory 
if [ -d ../build ]; then 
    cd ..
    rm -rf build
    mkdir build
    cd build
else
    echo "Ouch ... build directory DOES NOT exist"
    exit 1
fi

#  Compile 

cmake .. $OSM2PGSQL_CMPARAMS
make -j2

#  Test 
./osm2pgsql -V
#LSAN_OPTIONS=verbosity=1:log_threads=1 
ctest -VV ${RUNTEST}


echo "---------------------"
echo "GCCVER           : $GCCVER   [ $GCC_VERSION ] "
echo "cmake params     : $OSM2PGSQL_CMPARAMS "
echo "CXXFLAGS         : $CXXFLAGS"
echo "LUA_OPTION       : $LUA_OPTION"
echo "LUAJIT_OPTION    : $LUAJIT_OPTION"
echo "RUNTEST (filter) : $RUNTEST"
echo "----------------------"

echo " "
echo " ------------  end of build --------------"
echo " "
echo " "
echo " "
