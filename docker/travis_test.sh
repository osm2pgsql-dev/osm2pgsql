#!/usr/bin/env bash
set -Eeuo pipefail

# check docker versions!
docker version
docker-compose -v

# Travis Docker is sensitive - so "build" must exist!
mkdir -p build


# input parameters !  should fail if not exist! 
echo "------------------------------------"
echo " ----        testing!          -----"
echo "------------------------------------"
echo "GCCVER           : $GCCVER"
echo "CXXFLAGS         : $CXXFLAGS"
echo "LUA_OPTION       : $LUA_OPTION"
echo "LUAJIT_OPTION    : $LUAJIT_OPTION"
echo "RUNTEST (filter) : $RUNTEST"
echo "------------------------------------"


# Down- any running process ( for example local testing  ) 
docker-compose down


./docker/docker_build_gcc_image.sh
docker-compose run --rm osm2pgsql-dev ../docker/info.sh
docker-compose run \
        -e RUNTEST="$RUNTEST" \
        -e CXXFLAGS="$CXXFLAGS" \
        -e LUA_OPTION="$LUA_OPTION" \
        -e LUAJIT_OPTION="$LUAJIT_OPTION" \
        --rm osm2pgsql-dev \
        ../docker/build.sh

echo " -- end of test --- "
