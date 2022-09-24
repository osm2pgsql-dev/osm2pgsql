-- This config example file is released into the Public Domain.

-- Put all OSM data into a single table

-- inspect = require('inspect')

-- We define a single table that can take any OSM object and any geometry.
-- OSM nodes are converted to Points, ways to LineStrings and relations
-- to GeometryCollections. If an object would create an invalid geometry
-- it is still added to the table with a NULL geometry.
-- XXX expire will currently not work on these tables.
local dtable = osm2pgsql.define_table{
    name = "data",
    -- This will generate a column "osm_id INT8" for the id, and a column
    -- "osm_type CHAR(1)" for the type of object: N(ode), W(way), R(relation)
    ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
    columns = {
        { column = 'attrs', type = 'jsonb' },
        { column = 'tags',  type = 'jsonb' },
        { column = 'geom',  type = 'geometry' },
    }
}

-- print("columns=" .. inspect(dtable:columns()))

-- Helper function to remove some of the tags we usually are not interested in.
-- Returns true if there are no tags left.
function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

function process(object, geometry)
    if clean_tags(object.tags) then
        return
    end
    dtable:insert({
        attrs = {
            version = object.version,
            timestamp = object.timestamp,
        },
        tags = object.tags,
        geom = geometry
    })
end

function osm2pgsql.process_node(object)
    process(object, object:as_point())
end

function osm2pgsql.process_way(object)
    process(object, object:as_linestring())
end

function osm2pgsql.process_relation(object)
    process(object, object:as_geometrycollection())
end

