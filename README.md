# osm2pgsql #

https://osm2pgsql.org

osm2pgsql is a tool for loading OpenStreetMap data into a PostgreSQL / PostGIS
database suitable for applications like rendering into a map, geocoding with
Nominatim, or general analysis.

[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/7abwls7hfmb83axj/branch/master?svg=true)](https://ci.appveyor.com/project/openstreetmap/osm2pgsql/branch/master)
[![Github Actions Build Status](https://github.com/openstreetmap/osm2pgsql/workflows/CI/badge.svg?branch=master)](https://github.com/openstreetmap/osm2pgsql/actions)
[![Packaging Status](https://repology.org/badge/tiny-repos/osm2pgsql.svg)](https://repology.org/project/osm2pgsql/versions)

## Features ##

* Converts OSM files to a PostgreSQL DB
* Conversion of tags to columns is configurable in the style file
* Able to read .gz, .bz2, .pbf and .o5m files directly
* Can apply diffs to keep the database up to date
* Support the choice of output projection
* Configurable table names
* Gazetteer back-end for [Nominatim](https://wiki.openstreetmap.org/wiki/Nominatim)
* Support for hstore field type to store the complete set of tags in one database
  field if desired

## Installing ##

Most Linux distributions include osm2pgsql. It is also available on macOS with [Homebrew](https://brew.sh/).

Unoffical builds for Windows are available from [AppVeyor](https://ci.appveyor.com/project/openstreetmap/osm2pgsql/history) but you need to find the right build artifacts.
Builds for releases may also be downloaded from the [OpenStreetMap Dev server](https://lonvia.dev.openstreetmap.org/osm2pgsql-winbuild/releases/).

## Building ##

The latest source code is available in the osm2pgsql git repository on GitHub
and can be downloaded as follows:

```sh
$ git clone git://github.com/openstreetmap/osm2pgsql.git
```

Osm2pgsql uses the cross-platform [CMake build system](https://cmake.org/)
to configure and build itself and requires

Required libraries are

* [expat](https://libexpat.github.io/)
* [proj](https://proj.org/)
* [bzip2](http://www.bzip.org/)
* [zlib](https://www.zlib.net/)
* [Boost libraries](https://www.boost.org/), including system and filesystem
* [PostgreSQL](https://www.postgresql.org/) client libraries
* [Lua](https://www.lua.org/) (Optional, used for Lua tag transforms
  and the flex output)
* [Python](https://python.org/) (only for running tests)
* [Psycopg](http://initd.org/psycopg/) (only for running tests)

It also requires access to a database server running
[PostgreSQL](https://www.postgresql.org/) 9.3+ and
[PostGIS](http://www.postgis.net/) 2.2+.

Make sure you have installed the development packages for the libraries
mentioned in the requirements section and a C++ compiler which supports C++11.
GCC 5 and later and Clang 3.5 and later are known to work.

To rebuild the included man page you'll need the [pandoc](https://pandoc.org/)
tool.

First install the dependencies.

On a Debian or Ubuntu system, this can be done with:

```sh
sudo apt-get install make cmake g++ libboost-dev libboost-system-dev \
  libboost-filesystem-dev libexpat1-dev zlib1g-dev \
  libbz2-dev libpq-dev libproj-dev lua5.3 liblua5.3-dev pandoc
```

On a Fedora system, use

```sh
sudo dnf install cmake make gcc-c++ boost-devel expat-devel zlib-devel \
  bzip2-devel postgresql-devel proj-devel proj-epsg lua-devel pandoc
```

On RedHat / CentOS first run `sudo yum install epel-release` then install
dependencies with:

```sh
sudo yum install cmake make gcc-c++ boost-devel expat-devel zlib-devel \
  bzip2-devel postgresql-devel proj-devel proj-epsg lua-devel pandoc
```

On a FreeBSD system, use

```sh
pkg install devel/cmake devel/boost-libs textproc/expat2 \
  databases/postgresql94-client graphics/proj lang/lua52
```

Once dependencies are installed, use CMake to build the Makefiles in a separate folder

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

The compiled files can be installed with

```sh
sudo make install
```

By default, the Release build with debug info is created and no tests are compiled.
You can change that behavior by using additional options like following:

```sh
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

## Usage ##

This is only a short introduction. There is extensive documentation available
on [osm2pgsq.org](https://osm2pgsql.org/doc/).

Osm2pgsql has one program, the executable itself, which has a lot of command
line options.

Before loading into a database, the database must be created and the PostGIS
and optional hstore extensions must be loaded. A full guide to PostgreSQL
setup is beyond the scope of this readme, but with reasonably recent versions
of PostgreSQL and PostGIS this can be done with

```sh
createdb gis
psql -d gis -c 'CREATE EXTENSION postgis; CREATE EXTENSION hstore;'
```

A basic invocation to load the data into the database `gis` for rendering would be

```sh
osm2pgsql --create --database gis data.osm.pbf
```

This will load the data from `data.osm.pbf` into the `planet_osm_point`,
`planet_osm_line`, `planet_osm_roads`, and `planet_osm_polygon` tables.

When importing a large amount of data such as the complete planet, a typical
command line would be

```sh
osm2pgsql -c -d gis --slim -C <cache size> \
  --flat-nodes <flat nodes> planet-latest.osm.pbf
```
where
* `<cache size>` is about 75% of memory in MiB, to a maximum of about 30000. Additional RAM will not be used.
* `<flat nodes>` is a location where a 50GiB+ file can be saved.

Many different data files (e.g., .pbf) can be found at [planet.osm.org](https://planet.osm.org/).

The databases from either of these commands can be used immediately by
[Mapnik](https://mapnik.org/) for rendering maps with standard tools like
[renderd/mod_tile](https://github.com/openstreetmap/mod_tile),
[TileMill](https://tilemill-project.github.io/tilemill/), [Nik4](https://github.com/Zverik/Nik4),
among others.

## Alternate outputs (backends) ##

In addition to the standard pgsql output designed for rendering there is also
the gazetteer output for geocoding, principally with
[Nominatim](https://www.nominatim.org/), and the null output for testing.

Also available is the new flex output. It is much more flexible than the other
outputs. IT IS CURRENTLY EXPERIMENTAL AND SUBJECT TO CHANGE. The flex output is
only available if you have compiled osm2pgsql with Lua support.

## LuaJIT support ##

To speed up Lua tag transformations, [LuaJIT](https://luajit.org/) can be optionally
enabled on supported platforms. Performance measurements have shown about 25%
runtime reduction for a planet import, with about 40% reduction on parsing time.

On a Debian or Ubuntu system, this can be done with:

```sh
sudo apt install libluajit-5.1-dev
```

Configuration parameter `WITH_LUAJIT=ON` needs to be added to enable LuaJIT.
Otherwise make and installation steps are identical to the description above.

```sh
cmake -D WITH_LUAJIT=ON ..
```

Use `osm2pgsql --version` to verify that the build includes LuaJIT support:

```sh
./osm2pgsql --version
osm2pgsql version 1.2.0

Compiled using the following library versions:
Libosmium 2.15.6
Lua 5.1.4 (LuaJIT 2.1.0-beta3)
```

## Contributing ##

We welcome contributions to osm2pgsql. If you would like to report an issue,
please use the [issue tracker on GitHub](https://github.com/openstreetmap/osm2pgsql/issues).

More information can be found in [CONTRIBUTING.md](CONTRIBUTING.md).

General queries can be sent to the tile-serving@ or dev@
[mailing lists](https://wiki.openstreetmap.org/wiki/Mailing_lists).
