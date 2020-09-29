
local tables = {}

tables.line = osm2pgsql.define_way_table('osm2pgsql_test_line', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring', projection = 4326 }
})

tables.split = osm2pgsql.define_way_table('osm2pgsql_test_split', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring', projection = 4326 }
})

function osm2pgsql.process_way(object)
    tables.line:add_row({
        tags= object.tags,
        geom = { create = 'line' }
    })
    tables.split:add_row({
        tags = object.tags,
        geom = { create = 'line', split_at = 1.0 }
    })
end

