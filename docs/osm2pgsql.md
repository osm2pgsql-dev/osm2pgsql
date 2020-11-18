# NAME

osm2pgsql - Openstreetmap data to PostgreSQL converter

# SYNOPSIS

**osm2pgsql** \[*OPTIONS*\] OSM-FILE

# DESCRIPTION

**osm2pgsql** imports OpenStreetMap data into a PostgreSQL/PostGIS database. It
is an essential part of many rendering toolchains, the Nominatim geocoder and
other applications processing OSM data.

OSM planet snapshots can be downloaded from https://planet.openstreetmap.org/.
Data extracts for various countries or other areas are also available, see
https://wiki.openstreetmap.org/wiki/Planet.osm.

When operating in "slim" mode (and on a database created in "slim" mode!),
**osm2pgsql** can also process OSM change files (osc files), thereby bringing
an existing database up to date.

See the [manual](https://osm2pgsql.org/doc/manual.html) for more information.

# OPTIONS

This program follows the usual GNU command line syntax, with long options
starting with two dashes (`--`).

Mandatory arguments to long options are mandatory for short options too.

# MAIN OPTIONS

-a, \--append
:   Run in append mode. Adds the OSM change file into the database without
    removing existing data.

-c, \--create
:   Run in create mode. This is the default if **-a, \--append** is not
    specified. Removes existing data from the database tables!

# HELP/VERSION OPTIONS

-h, \--help
:   Print help. Add **-v, \--verbose** to display more verbose help.

-V, \--version
:   Print osm2pgsql version.

# LOGGING OPTIONS

\--log-level=LEVEL
:   Set log level ('debug', 'info' (default), 'warn', or 'error').

\--log-progress=VALUE
:   Enable (`true`) or disable (`false`) progress logging. The default is
    `auto` which will enable progress logging on the console and disable it
    if the output is redirected to a file.

\--log-sql
:   Enable logging of SQL commands for debugging.

\--log-sql-data
:   Enable logging of all data added to the database. This will write out
    a huge amount of data! For debugging.

-v, \--verbose
:   Same as `--log-level=debug`.

# DATABASE OPTIONS

-d, \--database=NAME
:   The name of the PostgreSQL database to connect to. If this parameter
    contains an `=` sign or starts with a valid URI prefix (`postgresql://` or
    `postgres://`), it is treated as a conninfo string. See the PostgreSQL
    manual for details.

-U, \--username=NAME
:   Postgresql user name.

-W, \--password
:   Force password prompt.

-H, \--host=HOSTNAME
:   Database server hostname or unix domain socket location.

-P, \--port=PORT
:   Database server port.

# INPUT OPTIONS

-r, \--input-reader=FORMAT
:   Select format of the input file. Available choices are **auto**
    (default) for autodetecting the format,
    **xml** for OSM XML format files, **o5m** for o5m formatted files
    and **pbf** for OSM PBF binary format.

-b, \--bbox=MINLON,MINLAT,MAXLON,MAXLAT
:   Apply a bounding box filter on the imported data. Example:
    **\--bbox** **-0.5,51.25,0.5,51.75**

# MIDDLE OPTIONS

-i, \--tablespace-index=TABLESPC
:   Store all indexes in a separate PostgreSQL tablespace named by this parameter.
    This allows one to e.g. store the indexes on faster storage like SSDs.

\--tablespace-slim-data=TABLESPC
:   Store the slim mode tables in the given tablespace.

\--tablespace-slim-index=TABLESPC
:   Store the indexes of the slim mode tables in the given tablespace.

-p, \--prefix=PREFIX
:   Prefix for table names (default: `planet_osm`).

-s, \--slim
:   Store temporary data in the database. Without this mode, all temporary data is stored in
    RAM and if you do not have enough the import will not work successfully. With slim mode,
    you should be able to import the data even on a system with limited RAM, although if you
    do not have enough RAM to cache at least all of the nodes, the time to import the data
    will likely be greatly increased.

\--drop
:   Drop the slim mode tables from the database once the import is complete. This can
    greatly reduce the size of the database, as the slim mode tables typically are the same
    size, if not slightly bigger than the main tables. It does not, however, reduce the
    maximum spike of disk usage during import. It can furthermore increase the import speed,
    as no indexes need to be created for the slim mode tables, which (depending on hardware)
    can nearly halve import time. Slim mode tables however have to be persistent if you want
    to be able to update your database, as these tables are needed for diff processing.

-C, \--cache=NUM
:   Only for slim mode: Use up to **NUM** MB of RAM for caching nodes. Giving osm2pgsql sufficient cache
    to store all imported nodes typically greatly increases the speed of the import. Each cached node
    requires 8 bytes of cache, plus about 10% - 30% overhead. As a rule of thumb,
    give a bit more than the size of the import file in PBF format. If the RAM is not
    big enough, use about 75% of memory. Make sure to leave enough RAM for PostgreSQL.
    It needs at least the amount of `shared_buffers` given in its configuration.
    Defaults to 800.

\--cache-strategy=STRATEGY
:   There are a number of different modes in which osm2pgsql can organize its
    node cache in RAM. These are optimized for different assumptions of the data
    and the hardware resources available. Currently available strategies are
    **dense**, **chunked**, **sparse** and **optimized**. **dense** assumes
    that the node id numbers are densely packed, i.e. only a few IDs in the range are
    missing / deleted. For planet extracts this is usually not the case, making the cache
    very inefficient and wasteful of RAM. **sparse** assumes node IDs in the data
    are not densely packed, greatly increasing caching efficiency in these cases.
    If node IDs are densely packed, like in the full planet, this strategy has a higher
    overhead for indexing the cache. **optimized** uses both dense and sparse strategies
    for different ranges of the ID space. On a block by block basis it tries to determine
    if it is more effective to store the block of IDs in sparse or dense mode. This is the
    default and should be typically used.

-x, \--extra-attributes
:   Include attributes for each object in the database.
    This includes the username, userid, timestamp and version.
    Note: this option also requires additional entries in your style file.

\--flat-nodes=FILENAME
:   The flat-nodes mode is a separate method to store slim mode node information on disk.
    Instead of storing this information in the main PostgreSQL database, this mode creates
    its own separate custom database to store the information. As this custom database
    has application level knowledge about the data to store and is not general purpose,
    it can store the data much more efficiently. Storing the node information for the full
    planet requires more than 300GB in PostgreSQL, the same data is stored in "only" 50GB using
    the flat-nodes mode. This can also increase the speed of applying diff files. This option
    activates the flat-nodes mode and specifies the location of the database file. It is a
    single large file. This mode is only recommended for full planet imports
    as it doesn't work well with small imports. The default is disabled.

\--middle-schema=SCHEMA
:   Use PostgreSQL schema SCHEMA for all tables, indexes, and functions in
    the middle (default is no schema, i.e. the `public` schema is used).

\--middle-way-node-index-id-shift=SHIFT
:   Set ID shift for way node bucket index in middle. Experts only. See
    documentation for details.

# OUTPUT OPTIONS

-O, \--output=OUTPUT
:   Specifies the output back-end to use. Currently osm2pgsql
    supports **pgsql**, **flex**, **gazetteer** and **null**. **pgsql** is
    the default output back-end and is optimized for rendering with Mapnik.
    **gazetteer** is intended for geocoding with Nominatim.
    The experimental **flex** backend allows more flexible configuration.
    **null** does not write any output and is only useful for testing or with
    **\--slim** for creating slim tables. There is also a **multi** backend. This is
    now deprecated and will be removed in future versions of osm2pgsql.

-S, \--style=FILE
:   The style file. This specifies how the data is imported into the database,
    its format depends on the output. (For the **pgsql** output, the default is
    `/usr/share/osm2pgsql/default.style`, for other outputs there is no
    default.)

# PGSQL OUTPUT OPTIONS

-i, \--tablespace-index=TABLESPACENAME
:   Store all indexes in a separate PostgreSQL tablespace named by this parameter.
    This allows one to e.g. store the indexes on faster storage like SSDs.

\--tablespace-main-data=TABLESPACENAME
:   Store the data tables (non slim) in the given tablespace.

\--tablespace-main-index=TABLESPACENAME
:   Store the indexes of the main tables (non slim) in the given tablespace.

\--latlong
:   Store data in degrees of latitude & longitude.

-m, \--merc
:   Store data in Spherical Mercator (Web Mercator, EPSG:3857) (the default).

-E, \--proj=SRID
:   Use projection EPSG:SRID.

-p, \--prefix=PREFIX
:   Prefix for table names (default: `planet_osm`). This option affects the
    middle as well as the pgsql output table names.

\--tag-transform-script=SCRIPT
:   Specify a lua script to handle tag filtering and normalisation. The script
    contains callback functions for nodes, ways and relations, which each take
    a set of tags and returns a transformed, filtered set of tags which are
    then written to the database.

-x, \--extra-attributes
:   Include attributes for each object in the database.
    This includes the username, userid, timestamp and version.
    Note: this option also requires additional entries in your style file.

-k, \--hstore
:   Add tags without column to an additional hstore (key/value) column to database tables.

-j, \--hstore-all
:   Add all tags to an additional hstore (key/value) column in database tables.

-z, \--hstore-column=KEY_PREFIX
:   Add an additional hstore (key/value) column containing all tags
    that start with the specified string, eg \--hstore-column "name:" will
    produce an extra hstore column that contains all `name:xx` tags.

\--hstore-match-only
:   Only keep objects that have a value in at least one of the non-hstore
    columns.

\--hstore-add-index
:   Create indexes for all hstore columns after import.

-G, \--multi-geometry
:   Normally osm2pgsql splits multi-part geometries into separate database rows per part.
    A single OSM id can therefore have several rows. With this option, osm2pgsql instead
    generates multi-geometry features in the PostgreSQL tables.

-K, \--keep-coastlines
:   Keep coastline data rather than filtering it out. By default objects
    tagged `natural=coastline` will be discarded based on the assumption that
    Shapefiles generated by OSMCoastline (https://osmdata.openstreetmap.de/)
    will be used for the coastline data.

\--reproject-area
:   Compute area column using spherical mercator coordinates.

\--output-pgsql-schema=SCHEMA
:   Use PostgreSQL schema SCHEMA for all tables, indexes, and functions in
    the pgsql and multi outputs (default is no schema, i.e. the `public` schema
    is used).

# EXPIRE OPTIONS

-e, \--expire-tiles=[MIN_ZOOM-]MAX-ZOOM
:   Create a tile expiry list.

-o, \--expire-output=FILENAME
:   Output file name for expired tiles list.

\--expire-bbox-size=SIZE
:   Max size for a polygon to expire the whole polygon, not just the boundary.

# ADVANCED OPTIONS

-I, \--disable-parallel-indexing
:   Disable parallel clustering and index building on all tables, build one
    index after the other.

\--number-processes=THREADS
:   Specifies the number of parallel threads used for certain operations.

\--with-forward-dependencies=BOOL
:   Propagate changes from nodes to ways and node/way members to relations
    (Default: `true`).

# SEE ALSO

* [osm2pgsql website](https://osm2pgsql.org)
* [osm2pgsql manual](https://osm2pgsql.org/doc/manual.html)
* **postgres**(1)
* **osmcoastline**(1)

