
local dtable = osm2pgsql.define_way_table('osm2pgsql_test_line', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring' },
}, { schema = 'myschema' })

local delete_keys = {
    'odbl',
    'created_by',
    'source'
}

local clean_tags = osm2pgsql.make_clean_tags_func(delete_keys)

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    dtable:add_row({
        tags = object.tags,
        geom = { create = 'line' }
    })
end

