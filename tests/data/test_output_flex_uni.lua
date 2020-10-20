
local test_table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data',
    ids = { type = 'any', type_column = 'x_type', id_column = 'x_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
    }
}

function is_empty(some_table)
    return next(some_table) == nil
end

function osm2pgsql.process_node(object)
    if is_empty(object.tags) then
        return
    end

    test_table:add_row({
        tags = object.tags,
        geom = { create = 'point' }
    })
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    if object.tags.building then
        test_table:add_row({
            tags = object.tags,
            geom = { create = 'area' }
        })
    else
        test_table:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.type == 'multipolygon' then
        test_table:add_row({
            tags = object.tags,
            geom = { create = 'area', multi = false }
        })
        return
    end
end

