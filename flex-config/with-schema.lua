-- This config example file is released into the Public Domain.

-- This configuration for the flex output shows how to define a table in
-- a PostgreSQL schema.
--
-- This config file expects that you have a schema called `myschema` in
-- your database (created with something like `CREATE SCHEMA myschema;`).

local dtable = osm2pgsql.define_way_table('data', {
        { column = 'tags',  type = 'jsonb' },
        { column = 'geom',  type = 'linestring' },
    }, { schema = 'myschema' })

function osm2pgsql.process_way(object)
    dtable:insert({
        tags = object.tags,
        geom = object:as_linestring()
    })
end

