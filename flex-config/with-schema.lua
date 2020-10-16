
-- This configuration for the flex output shows how to define a table in
-- a PostgreSQL schema.

local dtable = osm2pgsql.define_way_table('data', {
        { column = 'tags',  type = 'hstore' },
        { column = 'geom',  type = 'geometry' },
    }, { schema = 'myschema' })

function osm2pgsql.process_way(object)
    dtable:add_row({
        tags = object.tags,
        geom = { create = 'line' }
    })
end

