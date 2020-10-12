
-- Table with a single id column
local table1idcol = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data1',
    ids = { type = 'any', id_column = 'the_id' },
    columns = {
        { column = 'orig_id', type = 'int8' },
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
    }
}

-- Table with two id columns: type and id
local table2idcol = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data2',
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

    table1idcol:add_row({
        orig_id = object.id,
        tags = object.tags,
        geom = { create = 'point' }
    })
    table2idcol:add_row({
        tags = object.tags,
        geom = { create = 'point' }
    })
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    if object.tags.building then
        table1idcol:add_row({
            orig_id = object.id,
            tags = object.tags,
            geom = { create = 'area' }
        })
        table2idcol:add_row({
            tags = object.tags,
            geom = { create = 'area' }
        })
    else
        table1idcol:add_row({
            orig_id = object.id,
            tags = object.tags,
            geom = { create = 'line' }
        })
        table2idcol:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.type == 'multipolygon' then
        table1idcol:add_row({
            orig_id = object.id,
            tags = object.tags,
            geom = { create = 'area', multi = false }
        })
        table2idcol:add_row({
            tags = object.tags,
            geom = { create = 'area', multi = false }
        })
        return
    end
end

