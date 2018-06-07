#!/usr/bin/env bash
set -Eeuo pipefail

CXXFLAGS="-pedantic -Werror -fsanitize=address"
#CXXFLAGS="-Ofast"
#RUNTEST="-L NoDB"
RUNTEST="#"
LUA_OPTION="OFF"
LUAJIT_OPTION="OFF"

docker-compose down

for GCCVER in  4 5 6 7 8 ; do
  (
  export GCCVER
  export CXXFLAGS
  export RUNTEST
  export LUA_OPTION
  export LUAJIT_OPTION
  source ./docker/travis_test.sh
  )
done



exit

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
