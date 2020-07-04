
local rel_table = osm2pgsql.define_relation_table('osm2pgsql_test_relations', {
    { column = 'tags', type = 'hstore' }
})

function osm2pgsql.select_relation_members(relation)
    return { ways = osm2pgsql.way_member_ids(relation) }
end

function osm2pgsql.process_relation(object)
    rel_table:add_row({
        tags = object.tags
    })
end

