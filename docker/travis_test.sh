#!/usr/bin/env bash
set -Eeuo pipefail


# Travis Docker is sensitive - so "build" must exist!
mkdir -p build

# Down- any running process ( for example local testing  ) 
docker-compose down

echo "------------------------------------"
echo " ----  testing with gcc$GCCVER -----"
echo "------------------------------------"

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
