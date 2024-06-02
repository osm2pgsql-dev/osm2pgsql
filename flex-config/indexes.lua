-- This config example file is released into the Public Domain.

-- This file shows some options around index creation.

local tables = {}

-- When "indexes" is explicitly set to an empty Lua table, there will be no
-- index on this table. (The index for the id column is still built if
-- osm2pgsql needs that for updates.)
tables.pois = osm2pgsql.define_table({
    name = 'pois',
    ids = { type = 'node', id_column = 'node_id' },
    columns = {
        { column = 'tags', type = 'jsonb' },
        { column = 'geom', type = 'point', not_null = true },
    },
    indexes = {}
})

-- The "indexes" field is not set at all, you get the default, a GIST index on
-- the only (or first) geometry column.
tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', not_null = true },
})

-- Setting "indexes" explicitly: Two indexes area created, one on the polygon
-- geometry ("geom"), one on the center point geometry ("center"), both use
-- the GIST method.
tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'geometry', not_null = true },
    { column = 'center', type = 'point', not_null = true },
}, { indexes = {
    { column = 'geom', method = 'gist' },
    { column = 'center', method = 'gist' }
}})

-- You can put an index on any column, not just geometry columns, and use any
-- index method available in your PostgreSQL version. To get a list of methods
-- use: "SELECT amname FROM pg_catalog.pg_am WHERE amtype = 'i';"
tables.pubs = osm2pgsql.define_node_table('pubs', {
    { column = 'name', type = 'text' },
    { column = 'geom', type = 'geometry', not_null = true },
}, { indexes = {
    { column = 'geom', method = 'gist' },
    { column = 'name', method = 'btree' }
}})

-- You can also create indexes using multiple columns by specifying an array
-- as the "column". And you can add a where condition to the index. Note that
-- the content of the where condition is not checked, but given "as is" to
-- the database. You have to make sure it makes sense.
tables.roads = osm2pgsql.define_way_table('roads', {
    { column = 'name', type = 'text' },
    { column = 'type', type = 'text' },
    { column = 'ref', type = 'text' },
    { column = 'geom', type = 'linestring', not_null = true },
}, { indexes = {
    { column = { 'name', 'ref' }, method = 'btree' },
    { column = { 'geom' }, method = 'gist', where = "type='primary'" }
}})

-- Instead of on a column (or columns) you can define an index on an expression.
-- Indexes can be named (the default name is the one that PostgreSQL creates).
tables.postboxes = osm2pgsql.define_node_table('postboxes', {
    { column = 'operator', type = 'text' },
    { column = 'geom', type = 'point', not_null = true },
}, { indexes = {
    { expression = 'lower(operator)',
      method = 'btree',
      name = 'postbox_operator_idx' },
}})

-- Helper function that looks at the tags and decides if this is possibly
-- an area.
local function has_area_tags(tags)
    if tags.area == 'yes' then
        return true
    end
    if tags.area == 'no' then
        return false
    end

    return tags.aeroway
        or tags.amenity
        or tags.building
        or tags.harbour
        or tags.historic
        or tags.landuse
        or tags.leisure
        or tags.man_made
        or tags.military
        or tags.natural
        or tags.office
        or tags.place
        or tags.power
        or tags.public_transport
        or tags.shop
        or tags.sport
        or tags.tourism
        or tags.water
        or tags.waterway
        or tags.wetland
        or tags['abandoned:aeroway']
        or tags['abandoned:amenity']
        or tags['abandoned:building']
        or tags['abandoned:landuse']
        or tags['abandoned:power']
        or tags['area:highway']
end

function osm2pgsql.process_node(object)
    local geom = object:as_point()

    tables.pois:insert({
        tags = object.tags,
        geom = geom
    })

    if object.tags.amenity == 'pub' then
        tables.pubs:insert({
            name = object.tags.name,
            geom = geom
        })
    elseif object.tags.amenity == 'post_box' then
        tables.postboxes:insert({
            operator = object.tags.operator,
            geom = geom
        })
    end
end

function osm2pgsql.process_way(object)
    if object.is_closed and has_area_tags(object.tags) then
        local geom = object:as_polygon()
        tables.polygons:insert({
            tags = object.tags,
            geom = geom,
            center = geom:centroid()
        })
    else
        tables.ways:insert({
            tags = object.tags,
            geom = object:as_linestring()
        })
    end

    if object.tags.highway then
        tables.roads:insert({
            type = object.tags.highway,
            name = object.tags.name,
            ref = object.tags.ref,
            geom = object:as_linestring()
        })
    end
end

