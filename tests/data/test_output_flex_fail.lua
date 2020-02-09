
local table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'name', type = 'text' },
        { column = 'geom', type = 'geometry' },
        { column = 'area', type = 'area' },
    }
}

function is_empty(some_table)
    return next(some_table) == nil
end

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil
    tags['source:name'] = nil
end

function osm2pgsql.process_way(data)
    clean_tags(data.tags)
    if is_empty(data.tags) then
        return
    end

    table:add_row({
        tags = data.tags,
        name = data.tags.name,
    })
end

