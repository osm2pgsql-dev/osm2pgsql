-- This config example file is released into the Public Domain.

-- Use hstore column for storing all tags of nodes and ways. Adds a GIST index
-- for both tables that allows queries on the geometry *and* the tags.
--
-- See https://www.postgresql.org/docs/current/hstore.html#HSTORE-INDEXES
-- for details.
--
-- You need to enable the hstore extension for this to work:
-- CREATE EXTENSION hstore;

local nodes = osm2pgsql.define_table({
    name ='nodes',
    ids = { type = 'node', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'point' },
    },
    indexes = {
        { column = { 'geom', 'tags' }, method = 'gist' },
    },
})

local ways = osm2pgsql.define_table({
    name ='ways',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
    },
    indexes = {
        { column = { 'geom', 'tags' }, method = 'gist' },
    },
})

function osm2pgsql.process_node(object)
    nodes:insert({
        tags = object.tags,
        geom = object:as_point(),
    })
end

function osm2pgsql.process_way(object)
    local geom = object:as_polygon()

    if geom:is_null() then
        geom = object:as_linestring()
    end

    ways:insert({
        tags = object.tags,
        geom = geom,
    })
end

