
-- This is a very simple Lua config for the Flex Backend not intended for
-- real-world use. Look at and understand "simple.lua" first, before looking
-- at this file. This file will show some options around geometry processing.
-- After you have understood this file, go on to "data-types.lua".

local tables = {}

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'hstore' },
    -- Create a geometry column for point geometries. The geometry will be
    -- in web mercator, EPSG 3857.
    { column = 'geom', type = 'point' },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'hstore' },
    -- Create a geometry column for linestring geometries. The geometry will
    -- be in latlong (WGS84), EPSG 4326.
    { column = 'geom', type = 'linestring', projection = 4326 },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'geometry' },
    -- The 'area' type is used to store the calculated area of a polygon
    -- feature. This can be used in style sheets to only render larger polygons
    -- in small zoom levels. This will use the area in web mercator projection,
    -- you can set 'projection = 4326' to calculate the area in WGS84. Other
    -- projections are currently not supported.
    { column = 'area', type = 'area' },
})

tables.boundaries = osm2pgsql.define_relation_table('boundaries', {
    { column = 'type', type = 'text' },
    { column = 'tags', type = 'hstore' },
    -- Boundaries will be stitched together from relation members into long
    -- linestrings. This is a multilinestring column because sometimes the
    -- boundaries are not contiguous.
    { column = 'geom', type = 'multilinestring' },
})

-- Tables don't have to have a geometry column. This one will only collect
-- all the names of pubs but without any location information.
tables.pubs = osm2pgsql.define_node_table('pubs', {
    { column = 'name', type = 'text' }
})

-- Helper function to remove some of the tags we usually are not interested in.
-- Returns true if there are no tags left.
function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

-- Helper function that looks at the tags and decides if this is possibly
-- an area.
function has_area_tags(tags)
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
    if clean_tags(object.tags) then
        return
    end

    -- The 'geom' column is not mentioned here. So the default geometry
    -- transformation for a column of type 'point' will be used and the
    -- node location will be written as Point geometry into the database.
    tables.pois:add_row({
        tags = object.tags
    })

    if object.tags.amenity == 'pub' then
        tables.pubs:add_row({
            name = object.tags.name
        })
    end
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    -- A closed way that also has the right tags for an area is a polygon.
    if object.is_closed and has_area_tags(object.tags) then
        tables.polygons:add_row({
            tags = object.tags,
            -- The 'geom' column of the 'polygons' table is of type 'geometry'.
            -- There are several ways a way geometry could be converted to
            -- a geometry so you have to specify the geometry transformation.
            -- In this case we want to convert the way data to an area.
            geom = { create = 'area' }
        })
    else
        -- The 'geom' column of the 'ways' table is of type 'linestring'.
        -- Osm2pgsql knows how to create a linestring from a way, but
        -- if you want to specify extra parameters to this conversion,
        -- you have to do this explicitly. In this case we want to split
        -- long linestrings.
        --
        -- Set "split_at" to the maximum length the pieces should have. This
        -- length is in map units, so it depends on the projection used.
        -- "Traditional" osm2pgsql sets this to 1 for 4326 geometries and
        -- 100000 for 3857 (web mercator) geometries. The default is 0.0, which
        -- means no splitting.
        --
        -- Note that if a way is split this will automatically create
        -- multiple rows that are identical except for the geometry.
        tables.ways:add_row({
            tags = object.tags,
            geom = { create = 'line', split_at = 1 }
        })
    end
end

function osm2pgsql.process_relation(object)
    if clean_tags(object.tags) then
        return
    end

    local type = object:grab_tag('type')

    -- Store boundary relations as multilinestrings
    if type == 'boundary' then
        tables.boundaries:add_row({
            type = object:grab_tag('boundary'),
            tags = object.tags,
            -- For relations there is no clear definition what their geometry
            -- is, so you have to declare the geometry transformation
            -- explicitly.
            geom = { create = 'line' }
        })
        return
    end

    -- Store multipolygon relations as polygons
    if type == 'multipolygon' then
        tables.polygons:add_row({
            tags = object.tags,
            -- For relations there is no clear definition what their geometry
            -- is, so you have to declare the geometry transformation
            -- explicitly.
            geom = { create = 'area' }
        })
    end
end

