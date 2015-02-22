# osm2pgsql #

osm2pgsql is a tool for loading OpenStreetMap data into a PostgreSQL / PostGIS
database suitable for applications like rendering into a map, geocoding with
Nominatim, or general analysis.

## Features ##

* Converts OSM files to a PostgreSQL DB
* Conversion of tags to columns is configurable in the style file
* Able to read .gz, .bz2, .pbf and .o5m files directly
* Can apply diffs to keep the database up to date
* Support the choice of output projection
* Configurable table names
* Gazetteer back-end for [Nominatim](http://wiki.openstreetmap.org/wiki/Nominatim)
* Support for hstore field type to store the complete set of tags in one database
  field if desired

## Installing ##

The latest source code is available in the OSM git repository on github
and can be downloaded as follows:

```sh
$ git clone git://github.com/openstreetmap/osm2pgsql.git
```

## Building ##

Osm2pgsql uses the [GNU Build System](http://www.gnu.org/software/automake/manual/html_node/GNU-Build-System.html)
to configure and build itself and requires 

* [libxml2](http://xmlsoft.org/)
* [geos](http://geos.osgeo.org/)
* [proj](http://proj.osgeo.org/)
* [bzip2](http://www.bzip.org/)
* [zlib](http://www.zlib.net/)
* [Protocol Buffers](https://developers.google.com/protocol-buffers/)
* [PostgreSQL](http://www.postgresql.org/) client libraries
* [Lua](http://www.lua.org/) (Optional, used for [Lua tag transforms](docs/lua.md))

It also requires access to a database server running
[PostgreSQL](http://www.postgresql.org/) and [PostGIS](http://www.postgis.net/).

Make sure you have installed the development packages for the 
libraries mentioned in the requirements section and a C and C++
compiler.

To install on a Debian or Ubuntu system, first install the prerequisites:

```sh
sudo apt-get install autoconf automake libtool make g++ libboost-dev \
  libboost-system-dev libboost-filesystem-dev libboost-thread-dev libxml2-dev \
  libgeos-dev libgeos++-dev libpq-dev libbz2-dev libproj-dev
  protobuf-c-compiler libprotobuf-c0-dev lua5.2 liblua5.2-dev
```

To install on a Fedora system, use

```sh
sudo yum install gcc-c++ boost-devel libxml2-devel geos-devel \
  postgresql-devel bzip2-devel proj-devel protobuf-compiler
```

To install on a FreeBSD system, use

```sh
pkg install devel/git devel/autoconf devel/automake devel/gmake devel/libtool \
  textproc/libxml2 graphics/geos graphics/proj databases/postgresql94-client \
  devel/boost-libs devel/protobuf-c lang/lua52 devel/pkgconf
```

Then you should be able to bootstrap the build system:

    ./autogen.sh

And then run the standard GNU build install:

    ./configure && make && make install

Please see `./configure --help` for more options on how to control the build
process.

On FreeBSD instead bootstrap and then run

    LUA_LIB=`pkg-config --libs lua-5.2` ./configure --without-lockfree && gmake && gmake install

## Usage ##

Osm2pgsql has one program, the executable itself, which has **43** command line
options.

Before loading into a database, the database must be created and the PostGIS
and optionally hstore extensions must be loaded. A full guide to PostgreSQL
setup is beyond the scope of this readme, but with reasonably recent versions
of PostgreSQL and PostGIS this can be done with

```sh
createdb gis
psql -d gis -c 'CREATE EXTENSION postgis; CREATE EXTENSION hstore;'
```

A basic invocation to load the data into the database ``gis`` for rendering would be

```sh
osm2pgsql --create --database gis data.osm.pbf
```

This will load the data from ``data.osm.pbf`` into the ``planet_osm_point``,
``planet_osm_line``, ``planet_osm_roads``, and ``planet_osm_polygon`` tables.

When importing a large amount of data such as the complete planet, a typical
command line would be

```sh
osm2pgsql -c -d gis --slim -C <cache size> \
  --flat-nodes <flat nodes> planet-latest.osm.pbf
```
where
* ``<cache size>`` is 24000 on machines with 32GiB or more RAM
  or about 75% of memory in MiB on machines with less
* ``<flat nodes>`` is a location where a 24GiB file can be saved.

The databases from either of these commands can be used immediately by
[Mapnik](http://mapnik.org/) for rendering maps with standard tools like
[renderd/mod_tile](https://github.com/openstreetmap/mod_tile),
[TileMill](https://www.mapbox.com/tilemill/), [Nik4](https://github.com/Zverik/Nik4),
among others. It can also be used for [spatial analysis](docs/analysis.md) or
[shapefile exports](docs/export.md).

[Additional documentation is available on writing command lines](docs/usage.md).

## Alternate backends ##

In addition to the standard [pgsql](docs/pgsql.md) backend designed for
rendering there is also the [gazetteer](docs/gazetteer.md) database for
geocoding, principally with [Nominatim](http://www.nominatim.org/), and the
null backend for testing. For flexibility a new [multi](docs/multi.md)
backend is also avialable which allows the configuration of custom
postgres tables instead of those provided in the pgsql backend.

Any questions should be directed at the osm dev list
http://wiki.openstreetmap.org/index.php/Mailing_lists

## Testing ##

The code also comes with a suite of tests which can be run by
executing ``make check``.

Some of these tests depend on being able to set up a database and run
osm2pgsql against it. You need to ensure that PostgreSQL is running
and that your user is a superuser of that system. To do that, run:

```sh
sudo -u postgres createuser -s $USER
sudo mkdir -p /tmp/psql-tablespace
sudo chown postgres.postgres /tmp/psql-tablespace
psql -c "CREATE TABLESPACE tablespacetest LOCATION '/tmp/psql-tablespace'" postgres
```

Once this is all set up, all the tests should run (no SKIPs), and pass
(no FAILs). If you encounter a failure, you can find more information
by looking in the `test-suite.log`. If you find something which seems
to be a bug, please check to see if it is a known issue at
https://github.com/openstreetmap/osm2pgsql/issues and, if it's not
already known, report it there.

## Contributing ##

We welcome contributions to osm2pgsql. If you would like to report an issue,
please use the [issue tracker on GitHub](https://github.com/openstreetmap/osm2pgsql/issues).

General queries can be sent to the tile-serving@ or dev@
[mailing lists](http://wiki.openstreetmap.org/wiki/Mailing_lists).
