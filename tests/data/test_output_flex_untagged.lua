
local tables = {}

tables.nodes = osm2pgsql.define_node_table('osm2pgsql_test_nodes', {
    { column = 'tags', type = 'jsonb' },
    { column = 'tagged', type = 'bool' },
    { column = 'geom', type = 'point', not_null = true },
})

tables.ways = osm2pgsql.define_way_table('osm2pgsql_test_ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'tagged', type = 'bool' },
    { column = 'geom', type = 'linestring', not_null = true },
})

tables.relations = osm2pgsql.define_relation_table('osm2pgsql_test_relations', {
    { column = 'tags', type = 'jsonb' },
    { column = 'tagged', type = 'bool' },
    { column = 'geom', type = 'geometry', not_null = true },
})

local function insert(dtable, object, tagged, geom)
    tables[dtable]:insert({
        tags = object.tags,
        tagged = tagged,
        geom = geom,
    })
end

function osm2pgsql.process_node(object)
    insert('nodes', object, true, object:as_point())
end

function osm2pgsql.process_way(object)
    insert('ways', object, true, object:as_linestring())
end

function osm2pgsql.process_relation(object)
    insert('relations', object, true, object:as_geometrycollection())
end

function osm2pgsql.process_untagged_node(object)
    insert('nodes', object, false, object:as_point())
end

function osm2pgsql.process_untagged_way(object)
    insert('ways', object, false, object:as_linestring())
end

function osm2pgsql.process_untagged_relation(object)
    insert('relations', object, false, object:as_geometrycollection())
end

