
-- Table with a single id column
local table1idcol = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data1',
    ids = { type = 'any', id_column = 'the_id' },
    columns = {
        { column = 'orig_id', type = 'int8' },
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry', not_null = true },
    }
}

-- Table with two id columns: type and id
local table2idcol = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data2',
    ids = { type = 'any', type_column = 'x_type', id_column = 'x_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry', not_null = true },
    }
}

local function is_empty(some_table)
    return next(some_table) == nil
end

function osm2pgsql.process_node(object)
    if is_empty(object.tags) then
        return
    end

    table1idcol:insert({
        orig_id = object.id,
        tags = object.tags,
        geom = object:as_point()
    })
    table2idcol:insert({
        tags = object.tags,
        geom = object:as_point()
    })
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    if object.tags.building then
        table1idcol:insert({
            orig_id = object.id,
            tags = object.tags,
            geom = object:as_polygon()
        })
        table2idcol:insert({
            tags = object.tags,
            geom = object:as_polygon()
        })
    else
        table1idcol:insert({
            orig_id = object.id,
            tags = object.tags,
            geom = object:as_linestring()
        })
        table2idcol:insert({
            tags = object.tags,
            geom = object:as_linestring()
        })
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.type == 'multipolygon' then
        local mgeom = object:as_multipolygon()
        for sgeom in mgeom:geometries() do
            table1idcol:insert({
                orig_id = object.id,
                tags = object.tags,
                geom = sgeom
            })
            table2idcol:insert({
                tags = object.tags,
                geom = sgeom
            })
        end
    end
end

