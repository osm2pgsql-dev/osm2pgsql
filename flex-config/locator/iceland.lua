-- This config example file is released into the Public Domain.

-- This file shows how to use a locator with a bounding box to import only
-- the data for a region. In this case only highways in Iceland are imported
-- even if you run this on the full planet file.

local iceland = osm2pgsql.define_locator({ name = 'iceland' })

iceland:add_bbox('IS', -25.0, 62.0, -12.0, 68.0)

local highways = osm2pgsql.define_way_table('highways', {
    { column = 'hwtype', type = 'text', not_null = true },
    { column = 'name', type = 'text' },
    { column = 'ref', type = 'text' },
    { column = 'geom', type = 'linestring', not_null = true },
})

function osm2pgsql.process_way(object)
    local t = object.tags
    if t.highway then
        local geom = object:as_linestring()
        local region = iceland:first_intersecting(geom)
        if region then
            highways:insert({
                hwtype = t.highway,
                name = t.name,
                ref = t.ref,
                geom = geom,
            })
        end
    end
end
