-- This config example file is released into the Public Domain.

-- This is a very simple Lua config for the Flex output not intended for
-- real-world use. Look at and understand "simple.lua" first, before looking
-- at this file. This file will show some options around geometry processing.
-- After you have understood this file, go on to "data-types.lua".

local tables = {}

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'jsonb' },
    -- Create a geometry column for point geometries. The geometry will be
    -- in web mercator, EPSG 3857.
    --
    -- Usually we want to declare all geometry columns as "NOT NULL". If
    -- osm2pgsql encounters an invalid geometry (for whatever reason) it will
    -- generate a null geometry which will not be written to the database if
    -- "not_null" is set. The result is that broken geometries will just be
    -- silently ignored.
    { column = 'geom', type = 'point', not_null = true },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    -- Create a geometry column for linestring geometries. The geometry will
    -- be in latlong (WGS84), EPSG 4326.
    { column = 'geom', type = 'linestring', projection = 4326, not_null = true },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    -- If we don't set "not_null = true", we'll get NULL columns for invalid
    -- geometries. This can be useful if we want to detect those cases or
    -- if we have multiple geometry columns and some of them can be valid
    -- and others not.
    { column = 'geom', type = 'geometry', projection = 4326 },
    -- In this column we'll put the area calculated in Mercator coordinates
    { column = 'area', type = 'real' },
    -- In this column we'll put the true area calculated on the spheroid
    { column = 'spherical_area', type = 'real' },
})

tables.boundaries = osm2pgsql.define_relation_table('boundaries', {
    { column = 'type', type = 'text' },
    { column = 'tags', type = 'jsonb' },
    -- Boundaries will be stitched together from relation members into long
    -- linestrings. This is a multilinestring column because sometimes the
    -- boundaries are not contiguous.
    { column = 'geom', type = 'multilinestring', not_null = true },
})

-- Tables don't have to have a geometry column. This one will only collect
-- all the names of pubs but without any location information.
tables.pubs = osm2pgsql.define_node_table('pubs', {
    { column = 'name', type = 'text' }
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

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    local geom = object:as_point()

    tables.pois:insert({
        tags = object.tags,
        geom = geom -- the point will be automatically be projected to 3857
    })

    if object.tags.amenity == 'pub' then
        tables.pubs:insert({
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
        -- Creating the polygon geometry takes time, so we do it once here
        -- and later store it in the table and use it to calculate the area.
        local geom = object:as_polygon()
        tables.polygons:insert({
            tags = object.tags,
            geom = geom,
            -- Calculate the area in Mercator projection and store in the
            -- area column
            area = geom:transform(3857):area(),
            -- Also calculate "real" area in spheroid
            spherical_area = geom:spherical_area()
        })
    else
        -- We want to split long lines into smaller segments. We can use
        -- the "segmentize" function for that. The parameter specifies the
        -- maximum length the pieces should have. This length is in map units,
        -- so it depends on the projection used.
        -- "Traditional" osm2pgsql sets this to 1 for 4326 geometries and
        -- 100000 for 3857 (web mercator) geometries.
        --
        -- Because the result of the segmentation is a multigeometry, we'll
        -- have to iterate over all the member geometries to be able to insert
        -- the data into a the 'geom' column of the 'ways' table which is of
        -- type 'linestring'. (We could have used a multilinestring geometry
        -- in our table instead.)
        local multi_geom = object:as_multilinestring():segmentize(1)
        for g in multi_geom:geometries() do
            tables.ways:insert({
                tags = object.tags,
                geom = g
            })
        end
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
            type = object:grab_tag('boundary'),
            tags = object.tags,
            -- For relations there is no clear definition what their geometry
            -- is, so you have to decide on the geometry transformation you
            -- want. In this case we want the boundary as multilinestring
            -- and we want lines merged as much as possible.
            geom = object:as_multilinestring():line_merge()
        })
        return
    end

    -- Store multipolygon relations as polygons
    if relation_type == 'multipolygon' then
        local geom = object:as_multipolygon()
        tables.polygons:insert({
            tags = object.tags,
            -- For relations there is no clear definition what their geometry
            -- is, so you have to decide on the geometry transformation.
            -- In this case we know from the type tag its a (multi)polygon.
            geom = geom,
            -- Calculate the area in Mercator projection and store in the
            -- area column
            area = geom:transform(3857):area(),
            -- Also calculate "real" area in spheroid
            spherical_area = geom:spherical_area()
        })
    end
end

