# Pgsql Backend #

The pgsql backend is designed for rendering OpenStreetMap data, principally
with Mapnik, but is also useful for [analysis](analysis.md) and
[exporting](export.md) to other formats.

## Database Layout ##
It connects to a PostgreSQL database and stores the data in four tables

* ``planet_osm_point``
* ``planet_osm_line``
* ``planet_osm_roads``
* ``planet_osm_polygon``

planet_osm_roads contains the data from other tables, but has tags selected
for low-zoom rendering. It does not only contain roads.

The default prefix ``planet_osm`` can be changed with the ``--prefix`` option.

If you are using ``--slim`` mode, it will create the following additional 3
tables which are used by the pgsql middle layer, not the backend:

* ``planet_osm_nodes``
* ``planet_osm_ways``
* ``planet_osm_rels``

With the ``--flat-nodes`` option, the ``planet_osm_nodes`` information is
instead stored in a binary file.

**Note:** The names and structure of these additional tables, colloquially 
referred to as "slim tables", are an *internal implemention detail* of
osm2pgsql. While they do not usually change between releases of osm2pgsql,
be advised that if you rely on the content or layout of these tables in
your application, it is your responsibility to check whether your assumptions
are still true in a newer version of osm2pgsql before updating. See
https://github.com/openstreetmap/osm2pgsql/issues/230 for a discussion of
the topic.

## Importing ##

1. Runs a parser on the input file and processes the nodes, ways and relations.

2. If a node has a tag declared in the style file then it is added to
   ``planet_osm_point``. Regardless of tags, its position is stored by the
   middle layer.

3. If there are tags on a way in the style file as linear but without polygon
   tags, they are written into the lines and, depending on tags, roads tables.

   They are also stored by the middle layer.

4. Ways without tags or with polygon tags are stored as "pending" in the
   middle layer.

5. Relations are parsed. In this stage, "new-style" multipolygon and boundary
   relations are turned into polygons. Route relations are turned into
   linestrings.

6. "Pending" ways are processed, and they are either added as just the way, or
   if a member of a multipolygon relation, they processed as multipolygons.

7. Indexes are built. This may take substantial time, particularly for the
   middle layer indexes created in non-slim mode.
