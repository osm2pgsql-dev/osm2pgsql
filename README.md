# osm2pgsql

https://osm2pgsql.org

osm2pgsql is a tool for loading OpenStreetMap data into a PostgreSQL / PostGIS
database suitable for applications like rendering into a map, geocoding with
Nominatim, or general analysis.

See the [documentation](https://osm2pgsql.org/doc/) for instructions on how
to install and run osm2pgsql.

[![Github Actions Build Status](https://github.com/openstreetmap/osm2pgsql/workflows/CI/badge.svg?branch=master)](https://github.com/openstreetmap/osm2pgsql/actions)
[![Packaging Status](https://repology.org/badge/tiny-repos/osm2pgsql.svg)](https://repology.org/project/osm2pgsql/versions)

## Features

* freely configurable table layout and content using the LUA scripting language
* support for building geometries in simple feature format and for generalization
* read OSM data from OSM, PBF and O5M formats
* update the database from OSM change files and replication diffs
* usable for importing a small geographic area on a tiny laptop up to
  importing the whole world on a large server machine
* runs on Linux, Windows and MacOS
* [and more](https://osm2pgsql.org/about/features/)

## Installing

* __Linux__: most distributions include osm2pgsql. Debian always offers
  the latest version via backports.
* __macOS__: osm2pgsql is available through [Homebrew](https://brew.sh/)
* __Windows__: use our [binary builds](https://osm2pgsql.org/download/windows/)

For detailed instructions, visit
the [Installation page](https://osm2pgsql.org/doc/install.html).

## Building

The latest source code is available in the osm2pgsql git repository on GitHub
and can be downloaded as follows:

```sh
git clone https://github.com/osm2pgsql-dev/osm2pgsql.git
```

osm2pgsql uses the cross-platform [CMake build system](https://cmake.org/)
to configure and build itself.

Required libraries are

* [CLI11](https://github.com/CLIUtils/CLI11)
* [expat](https://libexpat.github.io/)
* [proj](https://proj.org/)
* [bzip2](http://www.bzip.org/)
* [zlib](https://www.zlib.net/)
* [Boost libraries](https://www.boost.org/) (for boost geometry)
* [nlohmann/json](https://json.nlohmann.me/)
* [PostgreSQL](https://www.postgresql.org/) client library
* [Lua](https://www.lua.org/)
* [OpenCV](https://opencv.org/) (optional, for generalization only)
* [potrace](https://potrace.sourceforge.net/) (optional, for generalization only)
* [Python](https://python.org/) (only for running tests)
* [Psycopg](https://www.psycopg.org/) (only for running tests)

The following libraries are included in the `contrib` directory. You can build
with other versions of those libraries (set the `EXTERNAL_*libname*` CMake
option to `ON`) but make sure you are using a compatible version:

* [fmt](https://fmt.dev/) (>= 7.1.3)
* [libosmium](https://osmcode.org/libosmium/) (>= 2.17.0)
* [protozero](https://github.com/mapbox/protozero) (>= 1.6.3)

It also requires access to a database server running
[PostgreSQL](https://www.postgresql.org/) (version 11+ works, 13+ strongly
recommended) and [PostGIS](https://www.postgis.net/) (version 3.0+).

Make sure you have installed the development packages for the libraries
mentioned in the requirements section and a C++ compiler which supports C++17.
We officially support gcc >= 10.0 and clang >= 13.

To rebuild the included man page you need the [pandoc](https://pandoc.org/)
tool.

First install the dependencies.

On a Debian or Ubuntu system, this can be done with:

```sh
sudo apt-get install make cmake g++ libboost-dev \
  libexpat1-dev zlib1g-dev libpotrace-dev \
  libopencv-dev libbz2-dev libpq-dev libproj-dev lua5.3 liblua5.3-dev \
  pandoc nlohmann-json3-dev pyosmium
```

On a Fedora system, use

```sh
sudo dnf install cmake make gcc-c++ libtool boost-devel bzip2-devel \
  expat-devel fmt-devel json-devel libpq-devel lua-devel zlib-devel \
  potrace-devel opencv-devel python3-osmium \
  postgresql-devel proj-devel proj-epsg pandoc
```

On RedHat / CentOS first run `sudo yum install epel-release` then install
dependencies with:

```sh
sudo yum install cmake make gcc-c++ boost-devel expat-devel zlib-devel \
  potrace-devel opencv-devel json-devel python3-osmium \
  bzip2-devel postgresql-devel proj-devel proj-epsg lua-devel pandoc
```

On a FreeBSD system, use

```sh
pkg install devel/cmake devel/boost-libs textproc/expat2 \
  databases/postgresql94-client graphics/proj lang/lua52
```

On Alpine, use

```sh
apk --update-cache add cmake make g++ nlohmann-json \
  postgresql-dev boost-dev expat-dev bzip2-dev zlib-dev \
  libpq proj-dev lua5.3-dev luajit-dev
```

Once dependencies are installed, use CMake to build the Makefiles in a separate
folder:

```sh
mkdir build && cd build
cmake ..
```

If some installed dependencies are not found by CMake, more options may need
to be set. Typically, setting `CMAKE_PREFIX_PATH` to a list of appropriate
paths is sufficient.

When the Makefiles have been successfully built, compile with

```sh
make
```

The man page can be rebuilt with:

```sh
make man
```

The compiled files can be installed with

```sh
sudo make install
```

By default, a Release build with debug info is created and tests are
disabled. You can change that behavior by using additional options like
following:

```sh
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

Note that `Debug` builds will be much slower than release build. For production
`Release` or `RelWithDebInfo` builds are recommended.

### Using the PROJ library

osm2pgsql has builtin support for the Latlong (WGS84, EPSG:4326) and the
WebMercator (EPSG:3857) projection. Other projections are supported through
the [Proj library](https://proj.org/). Support will be automatically compiled
in, when CMake can find the library in the system. Set the CMake
option `WITH_PROJ` to `OFF` to disable use of that library.

### Using LuaJIT

To speed up Lua tag transformations, [LuaJIT](https://luajit.org/) can be
optionally enabled on supported platforms. This can speed up processing
considerably.

On a Debian or Ubuntu system install the LuaJIT library:

```sh
sudo apt-get install libluajit-5.1-dev
```

Configuration parameter `WITH_LUAJIT=ON` needs to be added to enable LuaJIT.
Otherwise make and installation steps are identical to the description above.

```sh
cmake -D WITH_LUAJIT=ON ..
```

Use `osm2pgsql --version` to verify that the build includes LuaJIT support.
The output should show something like

```
Lua 5.1.4 (LuaJIT 2.1.0-beta3)
```

## Generalization

There is some experimental support for data generalization. See
https://osm2pgsql.org/generalization/ for details.

## Help/Support

If you have problems with osm2pgsql or want to report a bug, go to
https://osm2pgsql.org/support/ .

## License

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

## Maintainers

The current maintainers of osm2pgsql are [Sarah Hoffmann](https://github.com/lonvia/)
and [Paul Norman](https://github.com/pnorman/).

## Contributing

We welcome contributions to osm2pgsql. See [CONTRIBUTING.md](CONTRIBUTING.md)
and https://osm2pgsql.org/contribute/ for information on how to contribute.

