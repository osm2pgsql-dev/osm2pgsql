-- This config example file is released into the Public Domain.

-- This is Lua config showing the handling of custom geometries.
--
-- This is "work in progress", changes are possible.
--
-- Note that the insert() function is used instead of the old add_row()!

-- The following magic can be used to wrap the internal "insert" function
local orig_insert = osm2pgsql.Table.insert
local function my_insert(table, data)
    local json = require 'json'
    inserted, message, column, object = orig_insert(table, data)
    if not inserted then
        for key, value in pairs(data) do
            if type(value) == 'userdata' then
                data[key] = tostring(value)
            end
        end
        print("insert() failed: " .. message .. " column='" .. column .. "' object='" .. json.encode(object) .. "' data='" .. json.encode(data) .. "'")
    end
end
osm2pgsql.Table.insert = my_insert


local tables = {}

-- This table will get all nodes and areas with an "addr:street" tag, for
-- areas we'll calculate the centroid.
tables.addr = osm2pgsql.define_table({
    name = 'addr',
    ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
    columns = {
        { column = 'tags', type = 'jsonb' },
        { column = 'geom', type = 'point', projection = 4326 },
    }
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', projection = 4326 },
    { column = 'poly', type = 'polygon', projection = 4326 },
    { column = 'geom3857', type = 'linestring', projection = 3857 },
    { column = 'geomautoproj', type = 'linestring', projection = 3857 },
})

tables.major_roads = osm2pgsql.define_way_table('major_roads', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring' },
    { column = 'sgeom', type = 'linestring' }
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'geometry', not_null = true },
    { column = 'area4326', type = 'real' },
    { column = 'area3857', type = 'real' }
})

tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'multilinestring', projection = 4326 },
})

tables.route_parts = osm2pgsql.define_relation_table('route_parts', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', projection = 4326 },
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

    if object.tags['addr:street'] then
        tables.addr:insert({
           tags = object.tags,
           geom = object:as_point()
        })
    end
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    if object.is_closed and object.tags['addr:street'] then
        tables.addr:insert({
            tags = object.tags,
            geom = object:as_polygon():centroid()
        })
    end

    if object.tags.highway == 'motorway' or
       object.tags.highway == 'trunk' or
       object.tags.highway == 'primary' then
        tables.major_roads:insert({
            tags = object.tags,
            geom = object:as_linestring(),
            sgeom = object:as_linestring():simplify(100),
        })
    end

    -- A closed way that also has the right tags for an area is a polygon.
    if object.is_closed and has_area_tags(object.tags) then
        local g = object:as_polygon()
        local a = g:area()
        if a < 0.0000001 then
            tables.polygons:insert({
                tags = object.tags,
                geom = g,
                area4326 = a,
                area3857 = g:transform(3857):area()
            })
        end
    else
        tables.ways:insert({
            tags = object.tags,
            geom = object:as_linestring(),
            poly = object:as_polygon(),
            geom3857 = object:as_linestring():transform(3857), -- project geometry into target srid 3857
            geomautoproj = object:as_linestring() -- automatically projected into projection of target column
        })
    end
end

function osm2pgsql.process_relation(object)
    if clean_tags(object.tags) then
        return
    end

    local relation_type = object:grab_tag('type')

    if relation_type == 'multipolygon' then
        local g = object:as_multipolygon()
        local a = g:area()
        if a < 0.0000001 then
            tables.polygons:insert({
                tags = object.tags,
                geom = g,
                area4326 = a,
                area3857 = g:transform(3857):area()
            })
        end
        return
    end

    if relation_type == 'route' then
        local route_geom = object:as_multilinestring()
        if #route_geom > 0 then -- check that this is not a null geometry
--            print(object.id, tostring(route_geom), route_geom:srid(), #route_geom)
            tables.routes:insert({
                tags = object.tags,
                geom = route_geom
            })
            for n, line in pairs(route_geom:split_multi()) do
                tables.route_parts:insert({
                    tags = object.tags,
                    geom = line
                })
            end
        end
    end
end

