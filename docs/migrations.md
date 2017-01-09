# Migrations between versions #

Some osm2pgsql changes have slightly changed the database schema it expects. If
updating an old database, a migration may be needed. The migrations here assume
the default `planet_osm` prefix.

It is frequently better to reimport as this will also recluster the tables and
remove table or index bloat.

## 0.93 unprojected slim coordinates ##

The method of storing coordinates in the slim tables has changed. There is no
migration and a reload is required.

## 0.91 default projection ##

The default projection was moved from 900913 to 3857. This does not effect
users using `-l` or `-E`, but if using no projection options or `-m` a
migration is needed.

```sql
ALTER TABLE planet_osm_roads ALTER COLUMN way TYPE geometry(LineString,3857) USING ST_SetSRID(way,3857);
ALTER TABLE planet_osm_point ALTER COLUMN way TYPE geometry(Point,3857) USING ST_SetSRID(way,3857);
ALTER TABLE planet_osm_line ALTER COLUMN way TYPE geometry(LineString,3857) USING ST_SetSRID(way,3857);
ALTER TABLE planet_osm_polygon ALTER COLUMN way TYPE geometry(Geometry,3857) USING ST_SetSRID(way,3857);
```

## 0.88.0 z_order changes ##

0.88.0 z_order logic was changed, requuiring an increase in z_order values. To
migrate to the new range of values, run

```sql
UPDATE planet_osm_line SET z_order = z_order * 10;
UPDATE planet_osm_roads SET z_order = z_order * 10;
```

This will not apply the new logic, but will get the existing z_orders in the right
group of 100 for the new logic.

If not using osm2pgsql z_orders, this change may be ignored.

## 0.87.0 pending removal ##

0.87.0 moved the in-database tracking of pending ways and relations to
in-memory, for an increase in speed. This requires removal of the pending
column and a partial index associated with it.

```sql
ALTER TABLE planet_osm_ways DROP COLUMN pending;
ALTER TABLE planet_osm_rels DROP COLUMN pending;
```

## 32 bit to 64 bit ID migration ##

Old databases may have been imported with 32 bit node IDs, while current OSM
data requires 64 bit IDs. A database this old should not be migrated, but
reloaded. To migrate, the type of ID columns needs to be changed to `bigint`.
