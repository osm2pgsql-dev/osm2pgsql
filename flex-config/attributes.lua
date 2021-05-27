-- This config example file is released into the Public Domain.

-- This config shows how to access the attributes of OSM objects: the version,
-- changeset id, timestamp, user id and user name. For this to work the
-- command line option --extra-attributes/-x must be set, otherwise those
-- fields will be empty.

-- Set this to the projection you want to use
local srid = 4326

local tables = {}

tables.points = osm2pgsql.define_node_table('points', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'point', projection = srid },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    -- There is no built-in type for timestamps in osm2pgsql. So we use the
    -- PostgreSQL type "timestamp" and then have to convert our timestamps
    -- to a valid text representation for that type.
    { column = 'created', sql_type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
})

tables.lines = osm2pgsql.define_way_table('lines', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', projection = srid },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    { column = 'created', sql_type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
})

tables.relations = osm2pgsql.define_relation_table('relations', {
    { column = 'tags', type = 'jsonb' },
    { column = 'version', type = 'int' },
    { column = 'changeset', type = 'int' },
    { column = 'created', sql_type = 'timestamp' },
    { column = 'uid', type = 'int' },
    { column = 'user', type = 'text' },
})

function osm2pgsql.process_node(object)
    if next(object.tags) == nil then
        return
    end

    tables.points:add_row({
        tags = object.tags,
        version = object.version,
        changeset = object.changeset,
        created = os.date('!%Y-%m-%dT%H:%M:%SZ', object.timestamp),
        uid = object.uid,
        user = object.user
    })
end

function osm2pgsql.process_way(object)
    tables.lines:add_row({
        tags = object.tags,
        version = object.version,
        changeset = object.changeset,
        created = os.date('!%Y-%m-%dT%H:%M:%SZ', object.timestamp),
        uid = object.uid,
        user = object.user
    })
end

function osm2pgsql.process_relation(object)
    tables.relations:add_row({
        tags = object.tags,
        version = object.version,
        changeset = object.changeset,
        created = os.date('!%Y-%m-%dT%H:%M:%SZ', object.timestamp),
        uid = object.uid,
        user = object.user
    })
end

