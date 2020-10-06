
local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
    { column = 'tags',  type = 'hstore' },
    { column = 'min_x', type = 'real' },
    { column = 'min_y', type = 'real' },
    { column = 'max_x', type = 'real' },
    { column = 'max_y', type = 'real' },
    { column = 'geom',  type = 'point', projection = 4326 },
})

local highways = osm2pgsql.define_way_table('osm2pgsql_test_highways', {
    { column = 'tags',  type = 'hstore' },
    { column = 'min_x', type = 'real' },
    { column = 'min_y', type = 'real' },
    { column = 'max_x', type = 'real' },
    { column = 'max_y', type = 'real' },
    { column = 'geom',  type = 'linestring', projection = 4326 },
})

function osm2pgsql.process_node(object)
    local row = {
        tags = object.tags,
    }

    row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()

    points:add_row(row)
end

function osm2pgsql.process_way(object)
    local row = {
        tags = object.tags,
        geom = { create = 'line' }
    }

    row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()

    highways:add_row(row)
end

