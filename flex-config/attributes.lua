-- This config example file is released into the Public Domain.

-- This config shows how to access the attributes of OSM objects: the version,
-- changeset id, timestamp, user id and user name. Note that some OSM files do
-- not contain all of those attributes, so check your input data if you get
-- empty fields.

-- Set this to the projection you want to use
local srid = 4326

local tables = {}

tables.nodes = osm2pgsql.define_node_table('nodes', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'point', projection = srid },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    -- The timestamp of the object is available as a number (seconds since
    -- the epoch), osm2pgsql converts it for the "timestamp" column type.
    --
    -- Timestamps in OSM are always in UTC, depending on your use case you
    -- might want to store them using "timestamptz" instead.
    { column = 'created', type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', projection = srid },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    { column = 'created', type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
    { column = 'nodes', type = 'text', sql_type = 'bigint[]' },
})

tables.relations = osm2pgsql.define_relation_table('relations', {
    { column = 'tags', type = 'jsonb' },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    { column = 'created', type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
    { column = 'members', type = 'jsonb' },
})

function osm2pgsql.process_node(object)
    if next(object.tags) == nil then
        return
    end

    tables.nodes:insert({
        tags = object.tags,
        geom = object:as_point(),
        version = object.version,
        changeset = object.changeset,
        created = object.timestamp,
        uid = object.uid,
        user = object.user
    })
end

function osm2pgsql.process_way(object)
    tables.ways:insert({
        tags = object.tags,
        geom = object:as_linestring(),
        version = object.version,
        changeset = object.changeset,
        created = object.timestamp,
        uid = object.uid,
        user = object.user,
        nodes = '{' .. table.concat(object.nodes, ',') .. '}'
    })
end

function osm2pgsql.process_relation(object)
    tables.relations:insert({
        tags = object.tags,
        version = object.version,
        changeset = object.changeset,
        created = object.timestamp,
        uid = object.uid,
        user = object.user,
        members = object.members
    })
end

