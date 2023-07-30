-- This config example file is released into the Public Domain.
--
-- This example shows the use of the centroid() and pole_of_inaccessibility()
-- functions. For all named polygons several types of "center" points are
-- calculated that can be used for labelling.

local tables = {}

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'name', type = 'text' },
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'polygon', not_null = true },
    { column = 'centroid', type = 'point', not_null = true },
    { column = 'poi1', type = 'point', not_null = true },
    { column = 'poi2', type = 'point', not_null = true },
})

local function add(tags, geom)
    tables.polygons:insert({
        name = tags.name,
        tags = tags,
        geom = geom,
        -- The centroid is easy and fast to calculate
        centroid = geom:centroid(),
        -- The pole of inaccessibility can take a bit longer. It calculates
        -- the place in the polygon that's farthest away from the boundary.
        -- In other words it finds the center of the largest circle that fits
        -- completely into the polygon. (Note that due to the time required
        -- to calculate this, only an approximation will be calculated.)
        poi1 = geom:pole_of_inaccessibility(),
        -- The function allows a Lua table with options as parameter.
        -- Currently only a single option "stretch" is defined. If you set
        -- this to something other than 1 (the default), the functions doesn't
        -- use a circle but an ellipse stretched horizontally by the specified
        -- factor when calculating the result. This can make sense, because
        -- labels are usually written horizontally so you need more space in
        -- that direction. (Use a value > 1 to stretch horizontally or values
        -- between 0 and 1 to stretch vertically. The value 0 ist not allowed.)
        poi2 = geom:pole_of_inaccessibility({ stretch = 3 }),
    })
end

function osm2pgsql.process_way(object)
    if object.is_closed and object.tags.name then
        local geom = object:as_polygon()
        add(object.tags, geom)
    end
end

function osm2pgsql.process_relation(object)
    local relation_type = object:grab_tag('type')

    if relation_type == 'multipolygon' and object.tags.name then
        local geom = object:as_multipolygon()
        -- The pole_of_inaccessibility() function only works for polygons,
        -- not multipolygons. So we split up the multipolygons here and
        -- calculate the pole for each part separately.
        for g in geom:geometries() do
            add(object.tags, g)
        end
    end
end

