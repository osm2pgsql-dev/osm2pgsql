
local dtable = osm2pgsql.define_node_table('osm2pgsql_test_point', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
}, { schema = 'myschema' })

local delete_keys = {
    'odbl',
    'created_by',
    'source'
}

local clean_tags = osm2pgsql.make_clean_tags_func(delete_keys)

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    dtable:add_row({
        tags = object.tags
    })
end

