#!/usr/bin/env bash
set -Eeuo pipefail

# Local .travis test --  see  ".travis.yml" file!

#-- please don't leave empty ... just give a dummy flag!
CXXFLAGS="-pedantic -Werror"
#CXXFLAGS="-Ofast"

#RUNTEST: !!! please don't leave empty, just give a dummy ..
#RUNTEST="-L NoDB"
RUNTEST="#All"

LUA_OPTION="ON"
LUAJIT_OPTION="OFF"

# GCCVER = [4,5,6,7,8,7.1,7.2,7.3, ... ]  or any tags from here https://hub.docker.com/r/library/gcc/tags/
GCCVER=7
(
  export GCCVER
  export CXXFLAGS
  export RUNTEST
  export LUA_OPTION
  export LUAJIT_OPTION
  source ./docker/travis_test.sh
)


