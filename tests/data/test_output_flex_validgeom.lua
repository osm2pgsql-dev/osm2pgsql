
local polygons = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'geom', type = 'geometry' },
    }
}

function is_empty(some_table)
    return next(some_table) == nil
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    polygons:add_row({
        geom = { create = 'area' }
    })
end

function osm2pgsql.process_relation(object)
    polygons:add_row({
        geom = { create = 'area', multi = false }
    })
end

