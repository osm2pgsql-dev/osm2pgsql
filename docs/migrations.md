# Migrations between versions #

Some osm2pgsql changes have slightly changed the database schema it expects. If
updating an old database, a migration may be needed. The migrations here assume
the default `planet_osm` prefix.

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
