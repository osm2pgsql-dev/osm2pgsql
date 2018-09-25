#!/usr/bin/env bash

set -Eeuo pipefail

# build osm2pgsql-dev image
docker build \
           -f ./docker/Dockerfile-osm2pgsql-dev \
           -t osm2pgsql-dev:gcc${GCCVER:-7} \
           -t osm2pgsql-dev:latest \
           --build-arg gccver=${GCCVER:-7} \
           .

# List dev info - for debugging
#docker-compose run --rm -e gccver=${GCCVER:-7} osm2pgsql-dev ../docker/info.sh

#  List images
docker images  | grep -E 'gcc |mdillon/postgis |osm2pgsql-dev '