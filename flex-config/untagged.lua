-- This config example file is released into the Public Domain.
--
-- Most of the time we are only interested in nodes, ways, and relations that
-- have tags. But we can also get the untagged ones if necessary by using
-- the processing functions 'process_untagged_node', 'process_untagged_way',
-- and 'process_untagged_relation', respectively.

local nodes = osm2pgsql.define_node_table('nodes', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'point' },
})

local ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring' },
})

function osm2pgsql.process_node(object)
    nodes:insert({
        tags = object.tags,
        geom = object:as_point(),
    })
end

function osm2pgsql.process_untagged_node(object)
    nodes:insert({
        geom = object:as_point(),
    })
end

-- If you want to use the same function in both cases, that's also quite easy.
-- For instance like this:

local function do_way(object)
    ways:insert({
        tags = object.tags,
        geom = object:as_linestring(),
    })
end

osm2pgsql.process_way = do_way
osm2pgsql.process_untagged_way = do_way


