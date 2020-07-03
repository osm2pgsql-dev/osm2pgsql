
local test_table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
        { column = 'area', type = 'area' },
    }
}

function osm2pgsql.process_way(object)
    test_table:add_row({
        tags = object.tags
        -- missing geom transform here should lead to an error
    })
end

