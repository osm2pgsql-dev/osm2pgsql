# Exporting with osm2pgsql #

Osm2pgsql can be used in combination with [ogr2ogr](http://www.gdal.org/ogr2ogr.html) and a [PostgreSQL data source](http://www.gdal.org/drv_pg.html).

An example command to export to GeoJSON would be

    ogr2ogr -f "GeoJSON" roads.geojson -t_srs EPSG:4326 \
      PG:"dbname=gis" -s_srs EPSG:3857 \
      -sql "SELECT name,highway,oneway,toll,way FROM planet_osm_line WHERE highway IS NOT NULL"

Care should be taken if exporting to shapefiles, as characters may be present
which cannot be represented in ISO-8859-1, the standard encoding for shapefiles.
