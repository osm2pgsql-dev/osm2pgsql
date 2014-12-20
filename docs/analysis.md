# Geospatial analysis with osm2pgsql #

An osm2pgsql database and PostGIS is well-suited for geospatial analysis using
OpenStreetMap data where topology is not a consideration.

PostGIS provides an [extensive number of geometry functions](http://postgis.net/docs/manual-2.1/reference.html)
and a full description of how to perform analysis with them is beyond the
scope of a readme, but a simple example of finding the total road lengths by
classification for a municipality should help.

To start with, we'll download the data for the region as an [extract from Geofabrik](http://download.geofabrik.de/) and import it with osm2pgsql.

    osm2pgsql --database gis --number-processes 4 --multi-geometry british-columbia-latest.osm.pbf

``--multi-geometry`` (``-G``) is necessary for most analysis as it prevents
MULTIPOLYGONs from being split into multiple POLYGONs, a step that is
normally used to [increase rendering speed](http://paulnorman.ca/blog/2014/03/osm2pgsql-multipolygons)
but increases the complexity of analysis SQL.

Loading should take about 10 minutes, depending on computer speed. Once this
is done we'll open a PostgreSQL terminal with ``psql -d gis``, although a GUI
like pgadmin or any standard tool could be used instead.

To start, we'll create a partial index to speed up highway queries.

```sql
CREATE INDEX planet_osm_line_highways_index ON planet_osm_line USING GiST (way) WHERE (highway IS NOT NULL);
```

We'll first find the ID of the polygon we want

```sql
gis=# SELECT osm_id FROM planet_osm_polygon
WHERE boundary='administrative' AND admin_level='8' AND name='New Westminster';
  osm_id
----------
 -1377803
```

The negative sign tells us that the geometry is from a relation, and checking
on [the OpenStreetMap site](https://www.openstreetmap.org/relation/1377803)
confirms which it is.

We want to find all the roads in the city and get the length of the portion in
the city, sorted by road classification. Roads are in the ``planet_osm_line``
table, not the ``planet_osm_roads`` table which is only has a subset of data
for low-zoom rendering.

```sql
gis=# SELECT
    round(SUM(
      ST_Length(ST_Transform(
        ST_Intersection(way, (SELECT way FROM planet_osm_polygon WHERE osm_id=-1377803))
        ,4326)::geography)
    )) AS "distance (meters)", highway AS "highway type"
  FROM planet_osm_line
  WHERE highway IS NOT NULL
  AND ST_Intersects(way, (SELECT way FROM planet_osm_polygon WHERE osm_id=-1377803))
  GROUP BY highway
  ORDER BY "distance (meters)" DESC
  LIMIT 10;
 distance (meters) | highway type
-------------------+---------------
            138122 | residential
             79519 | service
             51890 | footway
             25610 | tertiary
             23434 | secondary
             14900 | cycleway
              6468 | primary
              5217 | motorway
              4389 | motorway_link
              3728 | track
```

The ``ST_Transform(...,4326)::geography`` is necessary because the data was
imported in Mercator. This step could have been avoided by importing in a local
projection like a suitable UTM projection.

More complicated analysises can be completed, but this simple example shows how
to use the tables and put conditions on the columns.