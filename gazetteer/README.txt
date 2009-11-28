gazetteer
=========
These scripts are used in conjunction with -O gazetter mode
of osm2pgsql to generate a database suitable for geo-coding

Changes
=======

Requirements
============
- PostgreSQL http://www.postgresql.org/
- PostGIS    http://postgis.refractions.net/
- PHP	     http://php.net/
- PHP-pgsql  
- PEAR::DB   http://pear.php.net/package/DB

Operation
=========

1) Make the database
createdb gazetteer
createlang plpgsql gazetteer
cat /usr/share/postgresql/8.3/contrib/_int.sql | psql gazetteer
cat /usr/share/postgresql/8.3/contrib/pg_trgm.sql | psql gazetteer
cat /usr/share/postgresql-8.3-postgis/lwpostgis.sql | psql gazetteer
cat /usr/share/postgresql-8.3-postgis/spatial_ref_sys.sql | psql gazetteer
./osm2pgsql -lsc -O gazetteer -d gazetteer planet.osm.bz2

2) Build the transliteration module
cd gazetter
make

Update gazetteer-functions.sql to give the absolute path to the module, 
replacing /home/twain/osm2pgsql/gazetteer/gazetteer.so

3) Various suplimentary data, used to patch holes in OSM data
cat import_worldboundaries.sql | psql gazetteer
cat import_country_name.sql | psql gazetteer
cat import_gb_postcode.sql | psql gazetteer
cat import_gb_postcodearea.sql | psql gazetteer
cat import_us_state.sql | psql gazetteer
cat import_us_statecounty.sql | psql gazetteer

4) Create website user (apache or www-user)
createuser -SDR www-data

5) Add gazetteer functions to database
cat gazetteer-functions.sql | psql gazetteer
cat gazetteer-tables.sql | psql gazetteer
cat gazetteer-functions.sql | psql gazetteer

6) Index the database - this will take a VERY long time!
cat gazetteer-index.sql | psql gazetteer
(Or use util.index.php, comment out the last 2 lines of gazetteer-index.sql)

7) Various 'special' words for searching - see file for details
cat import_specialwords.sql | psql gazetteer

7) setup the website
cp website/* ~/public_html/
