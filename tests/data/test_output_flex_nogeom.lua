
local pois = osm2pgsql.define_node_table('osm2pgsql_test_pois', {
    { column = 'tags',  type = 'hstore' },
})

function osm2pgsql.process_node(object)
    pois:add_row{ tags = object.tags }
end

