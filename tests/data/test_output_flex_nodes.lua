
local tables = {}

tables.t1 = osm2pgsql.define_node_table('osm2pgsql_test_t1', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
})

tables.t2 = osm2pgsql.define_node_table('osm2pgsql_test_t2', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
})

function osm2pgsql.process_node(object)
    if object.tags.t1 then
        tables.t1:insert({
            tags = object.tags,
            geom = object:as_point()
        })
    end
    if object.tags.t2 then
        tables.t2:insert({
            tags = object.tags,
            geom = object:as_point()
        })
    end
end

