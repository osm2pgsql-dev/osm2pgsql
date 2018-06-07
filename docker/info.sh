#!/usr/bin/env bash

set -Eeuo pipefail

# ========================================================================
echo " ----- Last git HEAD       : " && git rev-parse HEAD
echo " ----- Last git commit date: " && git log -1 --format=%cd 
gcc -v
cmake --version
xml2-config --version
lua -v
dpkg -l | grep -E 'lua|proj|xml|bz2|pq|zlib|boost|expat'

#todo list postgres/postgis - server version

