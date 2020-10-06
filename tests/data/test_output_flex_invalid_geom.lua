
local tables = {}

tables.line = osm2pgsql.define_table{
    name = 'osm2pgsql_test_line',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'linestring', projection = 4326 },
    }
}

tables.polygon = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry', projection = 4326 }
    }
}

function osm2pgsql.process_way(object)
    if not next(object.tags) then
        return
    end

    if object.tags.natural then
        tables.polygon:add_row({
            tags = object.tags,
            geom = { create = 'area' }
        })
    else
        tables.line:add_row({
            tags = object.tags
        })
    end
end

function osm2pgsql.process_relation(object)
    tables.polygon:add_row({
        tags = object.tags,
        geom = { create = 'area', multi = false }
    })
end

