gazetteer
=========
These scripts are used in conjunction with -O gazetter mode
of osm2pgsql to generate a database suitable for geo-coding

Changes
=======

Requirements
============
- GCC compiler	http://gcc.gnu.org/
- PostgreSQL 	http://www.postgresql.org/
- Proj4		http://trac.osgeo.org/proj/
- GEOS		http://trac.osgeo.org/geos/
- PostGIS    	http://postgis.refractions.net/
- PHP	     	http://php.net/ (both apache and command line)
- PHP-pgsql  
- PEAR::DB   	http://pear.php.net/package/DB
- wget

Hardware
========
For a full planet install you will need a minimum of 250GB of hard disk space.  On the OSM Nominatim server 
(http://wiki.openstreetmap.org/wiki/Servers/katie) the initial import (osm2pgsql) takes around 30 hours.
The rest of the indexing process takes approximately 10 days using both processors in parallel.

POSTGRESQL VERSION
==================
Please be aware that various problems have been found running Nominatim on PostgreSQL 8.4.  It is currently
recomended to use PostgreSQL 8.3 althought there are some reports that version 9.0 might have resolved
the issues.

Operation
=========
1) Make the database
createdb gazetteer
createlang plpgsql gazetteer
cat /usr/share/postgresql/8.3/contrib/_int.sql | psql gazetteer
# install location of /contrib and /postgis directories may differ on your machine
cat /usr/share/postgresql/8.3/contrib/pg_trgm.sql | psql gazetteer
cat /usr/share/postgresql-8.3-postgis/lwpostgis.sql | psql gazetteer
# lwpostgis.sql is replaced with postgis.sql in newer versions of postgis
cat /usr/share/postgresql-8.3-postgis/spatial_ref_sys.sql | psql gazetteer

2) Import OSM data
cd osm2pgsql
make
./osm2pgsql -lsc -O gazetteer -C 2000 -d gazetteer planet.osm.bz2
# -C 2000 uses 2000MB of memory as a cache, the more the better, especialy during the initial import
# No need to expand the planet file. osm2pgsql will handle the bzip.
# Ignore notices about missing functions and data types.
# If you get a projector initialization error, your proj installation can't
# be found in the expected location copying the proj folder to /usr/share/ will solve this.
# Be patient. Reading in the whole planet file takes a long time. By the end of 2009 you could expect these amounts:
#       Processing: Node(519844k) Way(38084k) Relation(316k) 

3) Build the transliteration module
cd gazetter
make

Update gazetteer-functions.sql to give the absolute path to the module, 
replacing /home/twain/osm2pgsql/gazetteer/gazetteer.so

4) Various suplimentary data, used to patch holes in OSM data
cd gazetteer
cat import_worldboundaries.sql | psql gazetteer
cat import_country_name.sql | psql gazetteer
cat import_gb_postcode.sql | psql gazetteer
cat import_gb_postcodearea.sql | psql gazetteer
cat import_us_state.sql | psql gazetteer
cat import_us_statecounty.sql | psql gazetteer

5) Create website user (apache or www-user)
createuser -SDR www-data

6) Add gazetteer functions to database
cat gazetteer-functions.sql | psql gazetteer
cat gazetteer-tables.sql | psql gazetteer
cat gazetteer-functions.sql | psql gazetteer
# You really do need to run gazetteer-functions.sql TWICE!

7) Copy the data into the live tables
# This does the first stange of indexing using various triggers and will take a while
cat gazetteer-loaddata.sql | psql gazetteer

8) Index the database - this will take a VERY long time! Approx 8 times the import time.
# For small imports (single country) use:
cat gazetteer-index.sql | psql gazetteer
# for anything large you will need to use util.update.php
./util.update.php --index
# if you have a multi processor system you can run multiple instances i.e.
./util.update.php --index --index-instances 3 --index-instance 0
./util.update.php --index --index-instances 3 --index-instance 1
./util.update.php --index --index-instances 3 --index-instance 2
# You will need to make sure settings.php is configured to connect to your database
# edit website/.htlib/settings.php

9) Various 'special' words for searching - see file for details
cat import_specialwords.sql | psql gazetteer

10) Setup the website
cp website/* ~/public_html/
# You will need to make sure settings.php is configured to connect to your database
# edit website/.htlib/settings.php

11) Updates
# update the table 'import_status' to reflect the date of your planet dump file (you
# might want to have a days overlap to ensure that no data is missed)
# edit util.update.php to replace /home/twain with the location you wish to store your files
./util.update.php --import-daily --import-all --index
