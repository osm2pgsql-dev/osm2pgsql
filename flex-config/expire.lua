-- This config example file is released into the Public Domain.
--
-- This examples shows how to use tile expiry.

local expire_outputs = {}

expire_outputs.pois = osm2pgsql.define_expire_output({
    -- The zoom level at which we calculate the tiles. This must always be set.
    maxzoom = 14,
    -- The filename where tile list should be written to.
    filename = 'pois.tiles'
})

expire_outputs.lines = osm2pgsql.define_expire_output({
    maxzoom = 14,
    -- Instead of writing the tile list to a file, it can be written to a table.
    -- The table will be created if it isn't there already.
    table = 'lines_tiles',
--    schema = 'myschema', -- You can also set a database schema.
})

expire_outputs.polygons = osm2pgsql.define_expire_output({
    -- You can also set a minimum zoom level in addition to the maximum zoom
    -- level. Tiles in all zoom levels between those two will be written out.
    minzoom = 10,
    maxzoom = 14,
    table = 'polygons_tiles'
})

print("Expire outputs:(")
for name, eo in pairs(expire_outputs) do
    print("  " .. name
          .. ": minzoom=" .. eo:minzoom()
          .. " maxzoom=" .. eo:maxzoom()
          .. " filename=" .. eo:filename()
          .. " schema=" .. eo:schema()
          .. " table=" .. eo:table())
end
print(")")

local tables = {}

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'jsonb' },
    -- Zero, one or more expire outputs are referenced in an `expire` field in
    -- the definition of any geometry column using the Web Mercator (3857)
    -- projection.
    { column = 'geom', type = 'point', not_null = true, expire = { { output = expire_outputs.pois } } },
})

tables.lines = osm2pgsql.define_way_table('lines', {
    { column = 'tags', type = 'jsonb' },
    -- If you only have a single expire output and with the default
    -- parameters, you can specify it directly.
    { column = 'geom', type = 'linestring', not_null = true, expire = expire_outputs.lines },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    -- In this configuration the `mode` of the expiry is set to `boundary-only`.
    -- Other modes are `full-area` (default) and `hybrid`. If set to `hybrid`
    -- you can set `full_area_limit` to a value in Web Mercator units. For
    -- polygons where the width and height of the bounding box is below this
    -- limit the full area is expired, for larger polygons only the boundary.
    -- This setting doesn't have any effect on point or linestring geometries.
    { column = 'geom', type = 'geometry', not_null = true, expire = {
        { output = expire_outputs.polygons, mode = 'boundary-only' }
    }}
})

tables.boundaries = osm2pgsql.define_relation_table('boundaries', {
    { column = 'type', type = 'text' },
    { column = 'tags', type = 'jsonb' },
    -- This geometry column doesn't have an `expire` field, so no expiry is
    -- done.
    { column = 'geom', type = 'multilinestring', not_null = true },
})

print("Tables:(")
for name, ts in pairs(tables) do
    print("  " .. name .. ": name=" .. ts:name() .. " (" .. tostring(ts) .. ")")
end
print(")")

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
    tables.pois:insert({
        tags = object.tags,
        geom = object:as_point()
    })
end

function osm2pgsql.process_way(object)
    if object.is_closed and has_area_tags(object.tags) then
        tables.polygons:insert({
            tags = object.tags,
            geom = object:as_polygon()
        })
    else
        tables.lines:insert({
            tags = object.tags,
            geom = object:as_linestring()
        })
    end
end

function osm2pgsql.process_relation(object)
    local relation_type = object:grab_tag('type')

    -- Store boundary relations as multilinestrings
    if relation_type == 'boundary' then
        tables.boundaries:insert({
            type = object:grab_tag('boundary'),
            tags = object.tags,
            geom = object:as_multilinestring():line_merge()
        })
        return
    end

    -- Store multipolygon relations as polygons
    if relation_type == 'multipolygon' then
        tables.polygons:insert({
            tags = object.tags,
            geom = object:as_multipolygon()
        })
    end
end

