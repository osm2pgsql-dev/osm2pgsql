-- This config example file is released into the Public Domain.
--
-- This config shows how to use the get_bbox() function to get the bounding
-- boxes of features.

local tables = {}

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'jsonb' },
    { column = 'bbox', type = 'text', sql_type = 'box2d' },
    { column = 'geom', type = 'point' },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'bbox', type = 'text', sql_type = 'box2d' },
    { column = 'geom', type = 'linestring' },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    { column = 'bbox', type = 'text', sql_type = 'box2d' },
    { column = 'geom', type = 'geometry' }
})

tables.boundaries = osm2pgsql.define_relation_table('boundaries', {
    { column = 'type', type = 'text' },
    { column = 'tags', type = 'jsonb' },
    { column = 'bbox', type = 'text', sql_type = 'box2d' },
    { column = 'geom', type = 'multilinestring' },
})

-- Helper function to remove some of the tags we usually are not interested in.
-- Returns true if there are no tags left.
local function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

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

-- Format the bounding box we get from calling get_bbox() on the parameter
-- in the way needed for the PostgreSQL/PostGIS box2d type.
local function format_bbox(object)
    local xmin, ymin, xmax, ymax = object:get_bbox()
    if xmin == nil then
        return nil
    end
    return 'BOX(' .. tostring(xmin) .. ' ' .. tostring(ymin)
           .. ',' .. tostring(xmax) .. ' ' .. tostring(ymax) .. ')'
end

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    tables.pois:insert({
        tags = object.tags,
        bbox = format_bbox(object),
        geom = object:as_point()
    })
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    -- A closed way that also has the right tags for an area is a polygon.
    if object.is_closed and has_area_tags(object.tags) then
        tables.polygons:insert({
            tags = object.tags,
            bbox = format_bbox(object),
            geom = object:as_polygon()
        })
    else
        tables.ways:insert({
            tags = object.tags,
            bbox = format_bbox(object),
            geom = object:as_linestring()
        })
    end
end

function osm2pgsql.process_relation(object)
    if clean_tags(object.tags) then
        return
    end

    local relation_type = object:grab_tag('type')

    -- Store boundary relations as multilinestrings
    if relation_type == 'boundary' then
        tables.boundaries:insert({
            type = object.tags.boundary,
            bbox = format_bbox(object),
            tags = object.tags,
            geom = object:as_multilinestring()
        })
        return
    end

    -- Store multipolygon relations as polygons
    if relation_type == 'multipolygon' then
        tables.polygons:insert({
            bbox = format_bbox(object),
            tags = object.tags,
            geom = object:as_multipolygon()
        })
    end
end

