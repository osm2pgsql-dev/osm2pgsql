Experimental osm2pgsql version 
==============================

svn.openstreetmap.org/utils/osm2pgsql/experimental

This version uses a direct database connection and generates 
a couple of temporary tables to store all the node and segment
data during the import. This keeps the memory usage at a roughly
constant 60MB during the whole procedure.

The database connrction parameters are hardcoded. It connects
to localhost with your current user name to the database "gis".
Change the "conninfo" string if you need to change this.

The tables used:

- tmp_nodes.
- tmp_segments.
Both these tables are created and destroyed automatically during 
the import

- planet_osm. Stores the output of the planet data in a form
suitable for mapnik.

Pros:
- Lower ram usage. On my machine 60MB for osm2pgsql, 120MB for
postmaster (the original osm2pgsql used > 1GB).
- No hard coded node/segment ID limits. Negative IDs too.
- Direct import into DB, no intermediate SQL file.

Cons:
- Slower. Currently takes 2 hours to convert the data instead of
around 20 minutes for the original osm2pgsql.
- Need to have postgres running during import
- Requires Postgesql C library 'libpq' 
('yum install postgresql-devel' or similar)

I'm working to see whether the speed can be improved. The postgres
docs suggest that COPY is the fastest import method. It will take some
further changes to implement this.

There is also a chance that the final version may have switches
to enable the old RAM storage + SQL output behaviours. I would also
like to support processing of OSM files stored in "way+segment+node"
order as produced by planetosm-excerpt-area.pl (currently osm2pgsql
relies on the OSM file being in "node+segment+way" order).


--- The readme.txt for the original version follows below ---


osm2pgsql
=========

Converts OSM planet.osm data to SQL suitable for loading into 
a PostgreSQL database and then rendered into tiles by Mapnik.

The format of the database is optimised for ease of rendering
by mapnik. It may be less suitable for other general purpose
processing.

For a broader view of the whole tile rendering tool chain see
http://wiki.openstreetmap.org/index.php/Slippy_Map

Any questions should be directed at the osm dev list
http://wiki.openstreetmap.org/index.php/Mailing_lists 



Operation
=========

1) Outputs SQL statements to create a new planet_osm table.

2) Runs an XML parser on the input file (typically planet.osm) 
and processes the nodes, segments and ways.

3) If a node has a tag declaring one of the attributes below then
 it is emitted in the SQL as a POINT. If it has no such tag then 
the position is noted, but not added to the SQL.

	name, place, landuse, waterway, highway, 
	railway, amenity, tourism, learning	

4) Segments are not output in the XML, they are used purely to 
locate the nodes during way processing.

5) Ways are read in and the segments are examined to determine
contiguous sequences by WKT(). Each sequence is emitted as a 
line of SQL. If way consists of several dis-joint sequences of
segments then multiple lines will be generated with the  
osm_id of the original way.

6) Ways with the tags landuse or leisure are emitted as using 
a POLYGON() geometry. Other ways are represented by using a 
LINESTRING().

7) Finally, more SQL is output to add a suitable index
and analyse the table to aid efficient querying.
