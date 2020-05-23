
-- This configuration for the flex output shows how to define a table in
-- a PostgreSQL schema.

local dtable = osm2pgsql.define_table{
    name = "data",
    schema = "myschema",
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'attrs', type = 'hstore' },
        { column = 'tags',  type = 'hstore' },
        { column = 'geom',  type = 'geometry' },
    }
}

function osm2pgsql.process_way(object)
    dtable:add_row({
        tags = object.tags,
        geom = { create = 'line' }
    })
end

