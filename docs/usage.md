# Command-line usage #

Osm2pgsql has one program, the executable itself, which has **43** command line
options. A full list of options can be obtained with ``osm2pgsql -h -v``. This
document provides an overview of options, and more importantly, why you might
use them.

## Overall options

* ``--append`` or ``--create`` specify if osm2pgsql is conducting a new import
  or adding to an existing one. ``--slim`` is required with ``--append``.

* ``--input-reader`` specifies the format if the filetype can't be
  automatically detected for some reason.

* ``--output`` specifies if the output backend is the default
  [`pgsql`](pgsql.md), the [`gazetteer`](gazetteer.md) output used by Nominatim,
  the new [`multi`](multi.md) backend which allows more customization of tables,
  or `null`, which emits no output to the backend.

  `null` will create slim tables if ``--slim`` is also used.

## Performance

Performance is heavily influenced by other options, but there are some options
that only impact performance.

* ``--cache`` specifies how much memory to allocate for caching information. In
  ``--slim`` mode, this is just node positions while in non-slim it has to
  store information about ways and relations too. The maximum RAM it is useful
  to set this to in slim mode is 8 bytes * number of nodes / efficiency, where
  efficiency ranges from 50% on small imports to 80% for a planet.

* ``--number-processes`` sets the number of processes to use. This should
  typically be set to the number of CPU threads, but gains in speed are minimal
  past 8 threads.

* ``--disable-parallel-indexing`` disables the clustering and indexing of all
  tables in parallel. This reduces disk and ram requirements during the import,
  but causes the last stages to take significantly longer.

* ``--cache-strategy`` sets the cache strategy to use. The defaults are fine
  here, and optimized uses less RAM than the other options.

## Database options ##

osm2pgsql supports standard options for how to connect to PostgreSQL. If left
unset, it will attempt to connect to the default database (usually the username)
using a unix socket. Most usage only requires setting ``--database``.

``--tablespace`` options allow the location of main and slim tables and indexes
to be set to different tablespaces independently, typically on machines with
multiple drive arrays where one is not large enough for all of the database.

``--flat-nodes`` specifies that instead of a table in PostgreSQL, a binary
file is used as a database of node locations. This should only be used on full
planet imports or very large extracts (e.g. Europe) but in those situations
offers significant space savings and speed increases, particularly on
mechanical drives. The file takes approximately 8 bytes * maximum node ID, or
about 23 GiB, regardless of the size of the extract.

``--prefix`` specifies the prefix for tables

## Middle-layer options ##

* ``--slim`` causes the middle layer to store node and way information in
  database rather than in memory. It is required for updates and for large
  extracts or the entire planet which will not fit in RAM.

* ``--drop`` discards the slim tables when they are no longer needed in the
  import, significantly reducing disk requirements and saving the time of
  building slim table indexes. A ``--slim --drop`` import is generally the
  fastest way to import the planet if updates are not required.

## Output columns options ##

### Column options

* ``--extra-attributes`` creates pseudo-tags with OSM meta-data like user,
  last edited, and changeset. These also need to be added to the style file.

* ``--style`` specifies the location of the style file. This defines what
  columns are created, what tags denote areas, and what tags can be ignored.
  The [default.style](../default.style) contains more documentation on this
  file.

* ``--tag-transform-script`` sets a [Lua tag transform](lua.md) to use in
  place of the built-in C tag transform.

### Hstore

Hstore is a [PostgreSQL data type](http://www.postgresql.org/docs/9.3/static/hstore.html)
that allows storing arbitrary key-value pairs. It needs to be installed on
the database with ``CREATE EXTENSION hstore;``

osm2pgsql has five hstore options

* ``--hstore`` or ``-k`` adds any tags not already in a conventional column to
  a hstore column. With the standard stylesheet this would result in tags like
  highway appearing in a conventional column while tags not in the style like
  ``name:en`` or ``lanes:forward`` would appear only in the hstore column.

* ``--hstore-all`` or ``-j`` adds all tags to a hstore column, even if they're
  already stored in a conventional column. With the standard stylesheet this
  would result in tags like highway appearing in conventional column and the
  hstore column while tags not in the style like ``name:en`` or
  ``lanes:forward`` would appear only in the hstore column.

* ``--hstore-column`` or ``-z``, which adds an additional column for tags
  starting with a specified string, e.g. ``--hstore-column 'name:'`` produces
  a hstore column that contains all ``name:xx`` tags

* ``--hstore-match-only`` modifies the above options and prevents objects from
  being added if they only have tags in the hstore column and no conventional
  tags.

* ``--hstore-add-index`` adds a GIN index to the hstore columns. This can
  speed up arbitrary queries, but for most purposes partial indexes will be
  faster.

Either ``--hstore`` or ``--hstore-all`` when combined with ``--hstore-match-only``
should give the same rows as no hstore, just with the additional hstore column.

Hstore is used to give more flexibility in using additional tags without
reimporting the database, at the cost of a
[less speed and more space.](http://paulnorman.ca/blog/2014/03/osm2pgsql-and-hstore/)

## Projection options

* ``--latlong``, ``--merc``, or ``--proj`` are used to specify the projection
  used for importing. The default, ``--merc`` is typically used for rendering,
  while ``--latlong`` can offer advantages for analysis. Most stylesheets
  assume ``--merc`` has been used.

## Output data options

* ``--multi-geometry`` skips an optimization for rendering where PostGIS
  MULTIPOLYGONs are split into multiple POLYGONs. ``--multi-geometry`` can be
  used to [avoid some labeling issues at the cost of speed](http://paulnorman.ca/blog/2014/03/osm2pgsql-multipolygons/).
  It is also typically required for [analysis](analysis.md).

* ``--keep-coastlines`` disables a hard-coded rule that would otherwise
  discard ``natural=coastline`` ways.
