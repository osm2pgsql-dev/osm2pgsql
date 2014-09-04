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
* Gazetteer back-end for Nominatim
  http://wiki.openstreetmap.org/wiki/Nominatim
* Support for hstore field type to store the complete set of tags in one database
  field if desired

## Installing ##

The latest source code is available in the OSM git repository on github
and can be downloaded as follows:

$ git clone git://github.com/openstreetmap/osm2pgsql.git

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
sudo apt-get install autoconf automake libtool make g++ libxml2-dev libgeos-dev
  libgeos++-dev libpq-dev libbz2-dev libproj-dev protobuf-c-compiler
  libprotobuf-c0-dev libbz2-dev lua5.2 liblua5.2-dev
```

To install on a Fedora system, use

```sh
sudo yum install gcc-c++ libxml2-devel geos-develpostgresql-devel bzip2-devel
  proj-devel protobuf-compiler
```

Then you should be able to bootstrap the build system:

    ./autogen.sh

And then run the standard GNU build install:

    ./configure && make && make install

Please see `./configure --help` for more options on how to control the build
process.

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
among others. It can also be used for [spatial analysis](docs/analysis.md) or [shapefile exports](docs/export.md).

[Additional documentation is available on writing command lines](docs/usage.md).

## Alternate backends ##

In addition to the standard [pgsql](docs/pgsql.md) backend designed for
rendering there is also the [gazetteer](docs/gazetteer.md) database for
geocoding, principally with [Nominatim](http://www.nominatim.org/), and the
null backend for testing.

Any questions should be directed at the osm dev list
http://wiki.openstreetmap.org/index.php/Mailing_lists

## Contributing ##

We welcome contributions to osm2pgsql. If you would like to report an issue,
please use the [issue tracker on GitHub](https://github.com/openstreetmap/osm2pgsql/issues).

General queries can be sent to the tile-serving@ or dev@ [mailing lists](http://wiki.openstreetmap.org/wiki/Mailing_lists).



Operation
=========

PostgreSQL 9.1 and PostGIS 2.0 or later are strongly suggested 
for databases in production. It is generally best to run the 
latest released versions if possible. PostgreSQL 8.4 and PostGIS 1.5 
will work but are substantially slower. Additionally, PostGIS 2.0 
contains enhancements that increase reliability as well as add new 
features that style sheet authors can use.

The default name for this database is 'gis' but this may
be changed by using the osm2pgsql --database option.

If the <username> matches the unix user id running the import
and rendering then this allows the PostgreSQL 'ident sameuser'
authentication to be used which avoids the need to enter a
password when accessing the database. This is setup by default
on many Unix installs but does not work on Windows (due to the
lack of unix sockets).

Some example commands are given below but you may find
this wiki page has more up to date information:
http://wiki.openstreetmap.org/wiki/Mapnik/PostGIS

Now you can run osm2pgsql to import the OSM data.
This will perform the following actions:

1) osm2pgsql connects to database and creates the following 4 tables
when used with the default output back-end (pgsql):
   - planet_osm_point
   - planet_osm_line
   - planet_osm_roads
   - planet_osm_polygon
The default prefix "planet_osm" can be changed with the --prefix option.

If you are using --slim mode, it will create the following additional 3 tables:
   - planet_osm_nodes
   - planet_osm_ways
   - planet_osm_rels

2) Runs a parser on the input file (typically planet-latest.osm.pbf)
 and processes the nodes, ways and relations.

3) If a node has a tag declared in the style file then it is 
 added to planet_osm_point. If it has no such tag then
 the position is noted, but not added to the database.

4) Ways are read in converted into WKT geometries by using the 
 positions of the nodes read in earlier. If the tags on the way 
 are listed in the style file then the way will be written into
 the line or roads tables.

5) If the way has one or more tags marked as 'polygon' and 
 forms a closed ring then it will be added to the planet_osm_polygon
 table.

6) The relations are parsed. Osm2pgsql has special handling for a
 limited number of types: multipolygon, route, boundary
 The code will build the appropriate geometries by referencing the
 members and outputting these into the database.

7) Indexes are added to speed up the queries by Mapnik.

Tuning PostgreSQL
=================

For an efficient operation of PostgreSQL you will need to tune the config
parameters of PostgreSQL from its default values. These are set in the
config file at /etc/postgresql/9.1/main/postgresql.conf

The values you need to set will depend on the hardware you have available,
but you will likely need to increase the values for the following parameters:

- shared_buffers
- checkpoint_segments
- work_mem
- maintenance_work_mem
- effective_cache_size


A quick note on projections
===========================

Depending on the command-line switches you can select which projection you
want the database in. You have three choices:

4326: The standard lat/long coordinates
900913: The spherical Mercator projection, used by TileCache, Google Earth etc.
3395: The legacy (broken) WGS84 Mercator projection

Depending on what you're using one or the other is appropriate. The default
Mapnik style (osm.xml) assumes that the data is stored in 900913 and this 
is the default for osm2pgsql.

Combining the -v and -h switches will tell about the exact definitions of
the projections.

In case you want to use some completely different projection there is the -E
option. It will initialize the projection as +init=epsg:<num>. This allows
you to use any projection recognized by proj4, which is useful if you want
to make a map in a different projection. These projections are usually
defined in /usr/share/proj/epsg.
