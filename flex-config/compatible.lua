
-- This configuration for the flex backend tries to be compatible with the
-- original pgsql backend.

-- Objects with any of the following keys will be treated as polygon
local polygon_keys = {
    'aeroway',
    'amenity',
    'building',
    'harbour',
    'historic',
    'landuse',
    'leisure',
    'man_made',
    'military',
    'natural',
    'office',
    'place',
    'power',
    'public_transport',
    'shop',
    'sport',
    'tourism',
    'water',
    'waterway',
    'wetland'
}

-- Objects without any of the following keys will be deleted
local generic_keys = {
    'access',
    'addr:housename',
    'addr:housenumber',
    'addr:interpolation',
    'admin_level',
    'aerialway',
    'aeroway',
    'amenity',
    'area',
    'barrier',
    'bicycle',
    'boundary',
    'brand',
    'bridge',
    'building',
    'capital',
    'construction',
    'covered',
    'culvert',
    'cutting',
    'denomination',
    'disused',
    'ele',
    'embarkment',
    'foot',
    'generation:source',
    'harbour',
    'highway',
    'historic',
    'hours',
    'intermittent',
    'junction',
    'landuse',
    'layer',
    'leisure',
    'lock',
    'man_made',
    'military',
    'motor_car',
    'name',
    'natural',
    'office',
    'oneway',
    'operator',
    'place',
    'population',
    'power',
    'power_source',
    'public_transport',
    'railway',
    'ref',
    'religion',
    'route',
    'service',
    'shop',
    'sport',
    'surface',
    'toll',
    'tourism',
    'tower:type',
    'tracktype',
    'tunnel',
    'type',
    'water',
    'waterway',
    'wetland',
    'width',
    'wood'
}

-- The following keys will be deleted
local delete_tags = {
    'FIXME',
    'note',
    'source',
    'way',
    'way_area',
    'z_order',
}

-- Array used to specify z_order per key/value combination.
-- Each element has the form {key, value, z_order, is_road}.
-- If is_road=1, the object will be added to planet_osm_roads.
local zordering_tags = {{ 'railway', nil, 5, 1}, { 'boundary', 'administrative', 0, 1},
    { 'bridge', 'yes', 10, 0 }, { 'bridge', 'true', 10, 0 }, { 'bridge', 1, 10, 0 },
    { 'tunnel', 'yes', -10, 0}, { 'tunnel', 'true', -10, 0}, { 'tunnel', 1, -10, 0},
    { 'highway', 'minor', 3, 0}, { 'highway', 'road', 3, 0 }, { 'highway', 'unclassified', 3, 0 },
    { 'highway', 'residential', 3, 0 }, { 'highway', 'tertiary_link', 4, 0}, { 'highway', 'tertiary', 4, 0},
    { 'highway', 'secondary_link', 6, 1}, { 'highway', 'secondary', 6, 1},
    { 'highway', 'primary_link', 7, 1}, { 'highway', 'primary', 7, 1},
    { 'highway', 'trunk_link', 8, 1}, { 'highway', 'trunk', 8, 1},
    { 'highway', 'motorway_link', 9, 1}, { 'highway', 'motorway', 9, 1},
}

local tables = {}

tables.point = osm2pgsql.define_table{
    name = 'planet_osm_point',
    ids = { type = 'node', id_column = 'osm_id' },
    columns = {
        { column = 'access', type = 'text' },
        { column = 'addr:housename', type = 'text' },
        { column = 'addr:housenumber', type = 'text' },
        { column = 'addr:interpolation', type = 'text' },
        { column = 'admin_level', type = 'text' },
        { column = 'aerialway', type = 'text' },
        { column = 'aeroway', type = 'text' },
        { column = 'amenity', type = 'text' },
        { column = 'area', type = 'text' },
        { column = 'barrier', type = 'text' },
        { column = 'bicycle', type = 'text' },
        { column = 'brand', type = 'text' },
        { column = 'bridge', type = 'text' },
        { column = 'boundary', type = 'text' },
        { column = 'building', type = 'text' },
        { column = 'capital', type = 'text' },
        { column = 'construction', type = 'text' },
        { column = 'covered', type = 'text' },
        { column = 'culvert', type = 'text' },
        { column = 'cutting', type = 'text' },
        { column = 'denomination', type = 'text' },
        { column = 'disused', type = 'text' },
        { column = 'ele', type = 'text' },
        { column = 'embankment', type = 'text' },
        { column = 'foot', type = 'text' },
        { column = 'generator:source', type = 'text' },
        { column = 'harbour', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'historic', type = 'text' },
        { column = 'horse', type = 'text' },
        { column = 'intermittent', type = 'text' },
        { column = 'junction', type = 'text' },
        { column = 'landuse', type = 'text' },
        { column = 'layer', type = 'text' },
        { column = 'leisure', type = 'text' },
        { column = 'lock', type = 'text' },
        { column = 'man_made', type = 'text' },
        { column = 'military', type = 'text' },
        { column = 'motorcar', type = 'text' },
        { column = 'name', type = 'text' },
        { column = 'natural', type = 'text' },
        { column = 'office', type = 'text' },
        { column = 'oneway', type = 'text' },
        { column = 'operator', type = 'text' },
        { column = 'place', type = 'text' },
        { column = 'population', type = 'text' },
        { column = 'power', type = 'text' },
        { column = 'power_source', type = 'text' },
        { column = 'public_transport', type = 'text' },
        { column = 'railway', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'religion', type = 'text' },
        { column = 'route', type = 'text' },
        { column = 'service', type = 'text' },
        { column = 'shop', type = 'text' },
        { column = 'sport', type = 'text' },
        { column = 'surface', type = 'text' },
        { column = 'toll', type = 'text' },
        { column = 'tourism', type = 'text' },
        { column = 'tower:type', type = 'text' },
        { column = 'tunnel', type = 'text' },
        { column = 'water', type = 'text' },
        { column = 'waterway', type = 'text' },
        { column = 'wetland', type = 'text' },
        { column = 'width', type = 'text' },
        { column = 'wood', type = 'text' },
        { column = 'z_order', type = 'int' },
        { column = 'way', type = 'point' },
    }
}

tables.line = osm2pgsql.define_table{
    name = 'planet_osm_line',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = {
        { column = 'access', type = 'text' },
        { column = 'addr:housename', type = 'text' },
        { column = 'addr:housenumber', type = 'text' },
        { column = 'addr:interpolation', type = 'text' },
        { column = 'admin_level', type = 'text' },
        { column = 'aerialway', type = 'text' },
        { column = 'aeroway', type = 'text' },
        { column = 'amenity', type = 'text' },
        { column = 'area', type = 'text' },
        { column = 'barrier', type = 'text' },
        { column = 'bicycle', type = 'text' },
        { column = 'brand', type = 'text' },
        { column = 'bridge', type = 'text' },
        { column = 'boundary', type = 'text' },
        { column = 'building', type = 'text' },
        { column = 'construction', type = 'text' },
        { column = 'covered', type = 'text' },
        { column = 'culvert', type = 'text' },
        { column = 'cutting', type = 'text' },
        { column = 'denomination', type = 'text' },
        { column = 'disused', type = 'text' },
        { column = 'embankment', type = 'text' },
        { column = 'foot', type = 'text' },
        { column = 'generator:source', type = 'text' },
        { column = 'harbour', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'historic', type = 'text' },
        { column = 'horse', type = 'text' },
        { column = 'intermittent', type = 'text' },
        { column = 'junction', type = 'text' },
        { column = 'landuse', type = 'text' },
        { column = 'layer', type = 'text' },
        { column = 'leisure', type = 'text' },
        { column = 'lock', type = 'text' },
        { column = 'man_made', type = 'text' },
        { column = 'military', type = 'text' },
        { column = 'motorcar', type = 'text' },
        { column = 'name', type = 'text' },
        { column = 'natural', type = 'text' },
        { column = 'office', type = 'text' },
        { column = 'oneway', type = 'text' },
        { column = 'operator', type = 'text' },
        { column = 'place', type = 'text' },
        { column = 'population', type = 'text' },
        { column = 'power', type = 'text' },
        { column = 'power_source', type = 'text' },
        { column = 'public_transport', type = 'text' },
        { column = 'railway', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'religion', type = 'text' },
        { column = 'route', type = 'text' },
        { column = 'service', type = 'text' },
        { column = 'shop', type = 'text' },
        { column = 'sport', type = 'text' },
        { column = 'surface', type = 'text' },
        { column = 'toll', type = 'text' },
        { column = 'tourism', type = 'text' },
        { column = 'tower:type', type = 'text' },
        { column = 'tracktype', type = 'text' },
        { column = 'tunnel', type = 'text' },
        { column = 'water', type = 'text' },
        { column = 'waterway', type = 'text' },
        { column = 'wetland', type = 'text' },
        { column = 'width', type = 'text' },
        { column = 'wood', type = 'text' },
        { column = 'z_order', type = 'int' },
        { column = 'way_area', type = 'area' },
        { column = 'way', type = 'linestring', split_at = 100000 },
    }
}

tables.polygon = osm2pgsql.define_table{
    name = 'planet_osm_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'access', type = 'text' },
        { column = 'addr:housename', type = 'text' },
        { column = 'addr:housenumber', type = 'text' },
        { column = 'addr:interpolation', type = 'text' },
        { column = 'admin_level', type = 'text' },
        { column = 'aerialway', type = 'text' },
        { column = 'aeroway', type = 'text' },
        { column = 'amenity', type = 'text' },
        { column = 'area', type = 'text' },
        { column = 'barrier', type = 'text' },
        { column = 'bicycle', type = 'text' },
        { column = 'brand', type = 'text' },
        { column = 'bridge', type = 'text' },
        { column = 'boundary', type = 'text' },
        { column = 'building', type = 'text' },
        { column = 'construction', type = 'text' },
        { column = 'covered', type = 'text' },
        { column = 'culvert', type = 'text' },
        { column = 'cutting', type = 'text' },
        { column = 'denomination', type = 'text' },
        { column = 'disused', type = 'text' },
        { column = 'embankment', type = 'text' },
        { column = 'foot', type = 'text' },
        { column = 'generator:source', type = 'text' },
        { column = 'harbour', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'historic', type = 'text' },
        { column = 'horse', type = 'text' },
        { column = 'intermittent', type = 'text' },
        { column = 'junction', type = 'text' },
        { column = 'landuse', type = 'text' },
        { column = 'layer', type = 'text' },
        { column = 'leisure', type = 'text' },
        { column = 'lock', type = 'text' },
        { column = 'man_made', type = 'text' },
        { column = 'military', type = 'text' },
        { column = 'motorcar', type = 'text' },
        { column = 'name', type = 'text' },
        { column = 'natural', type = 'text' },
        { column = 'office', type = 'text' },
        { column = 'oneway', type = 'text' },
        { column = 'operator', type = 'text' },
        { column = 'place', type = 'text' },
        { column = 'population', type = 'text' },
        { column = 'power', type = 'text' },
        { column = 'power_source', type = 'text' },
        { column = 'public_transport', type = 'text' },
        { column = 'railway', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'religion', type = 'text' },
        { column = 'route', type = 'text' },
        { column = 'service', type = 'text' },
        { column = 'shop', type = 'text' },
        { column = 'sport', type = 'text' },
        { column = 'surface', type = 'text' },
        { column = 'toll', type = 'text' },
        { column = 'tourism', type = 'text' },
        { column = 'tower:type', type = 'text' },
        { column = 'tracktype', type = 'text' },
        { column = 'tunnel', type = 'text' },
        { column = 'water', type = 'text' },
        { column = 'waterway', type = 'text' },
        { column = 'wetland', type = 'text' },
        { column = 'width', type = 'text' },
        { column = 'wood', type = 'text' },
        { column = 'z_order', type = 'int' },
        { column = 'way_area', type = 'area' },
        { column = 'way', type = 'geometry' },
    }
}

tables.roads = osm2pgsql.define_table{
    name = 'planet_osm_roads',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = {
        { column = 'access', type = 'text' },
        { column = 'addr:housename', type = 'text' },
        { column = 'addr:housenumber', type = 'text' },
        { column = 'addr:interpolation', type = 'text' },
        { column = 'admin_level', type = 'text' },
        { column = 'aerialway', type = 'text' },
        { column = 'aeroway', type = 'text' },
        { column = 'amenity', type = 'text' },
        { column = 'area', type = 'text' },
        { column = 'barrier', type = 'text' },
        { column = 'bicycle', type = 'text' },
        { column = 'brand', type = 'text' },
        { column = 'bridge', type = 'text' },
        { column = 'boundary', type = 'text' },
        { column = 'building', type = 'text' },
        { column = 'construction', type = 'text' },
        { column = 'covered', type = 'text' },
        { column = 'culvert', type = 'text' },
        { column = 'cutting', type = 'text' },
        { column = 'denomination', type = 'text' },
        { column = 'disused', type = 'text' },
        { column = 'embankment', type = 'text' },
        { column = 'foot', type = 'text' },
        { column = 'generator:source', type = 'text' },
        { column = 'harbour', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'historic', type = 'text' },
        { column = 'horse', type = 'text' },
        { column = 'intermittent', type = 'text' },
        { column = 'junction', type = 'text' },
        { column = 'landuse', type = 'text' },
        { column = 'layer', type = 'text' },
        { column = 'leisure', type = 'text' },
        { column = 'lock', type = 'text' },
        { column = 'man_made', type = 'text' },
        { column = 'military', type = 'text' },
        { column = 'motorcar', type = 'text' },
        { column = 'name', type = 'text' },
        { column = 'natural', type = 'text' },
        { column = 'office', type = 'text' },
        { column = 'oneway', type = 'text' },
        { column = 'operator', type = 'text' },
        { column = 'place', type = 'text' },
        { column = 'population', type = 'text' },
        { column = 'power', type = 'text' },
        { column = 'power_source', type = 'text' },
        { column = 'public_transport', type = 'text' },
        { column = 'railway', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'religion', type = 'text' },
        { column = 'route', type = 'text' },
        { column = 'service', type = 'text' },
        { column = 'shop', type = 'text' },
        { column = 'sport', type = 'text' },
        { column = 'surface', type = 'text' },
        { column = 'toll', type = 'text' },
        { column = 'tourism', type = 'text' },
        { column = 'tower:type', type = 'text' },
        { column = 'tracktype', type = 'text' },
        { column = 'tunnel', type = 'text' },
        { column = 'water', type = 'text' },
        { column = 'waterway', type = 'text' },
        { column = 'wetland', type = 'text' },
        { column = 'width', type = 'text' },
        { column = 'wood', type = 'text' },
        { column = 'z_order', type = 'int' },
        { column = 'way_area', type = 'area' },
        { column = 'way', type = 'linestring', split_at = 100000 },
    }
}

function get_z_order(keyvalues)
    -- The default z_order is 0
    local z_order = 0
    local roads = false

    -- Add the value of the layer key times 10 to z_order
    if (keyvalues.layer ~= nil and tonumber(keyvalues.layer)) then
        z_order = 10*keyvalues.layer
    end

   -- Increase or decrease z_order based on the specific key/value combination as specified in zordering_tags
    for i,k in ipairs(zordering_tags) do
        -- If the value in zordering_tags is specified, match key and value. Otherwise, match key only.
        if ((k[2]  and keyvalues[k[1]] == k[2]) or (k[2] == nil and keyvalues[k[1]] ~= nil)) then
            -- If the fourth component of the element of zordering_tags is 1, add the object to planet_osm_roads
            if (k[4] == 1) then
                roads = true
            end
            z_order = z_order + k[3]
        end
    end

    return z_order, roads
end

-- Helper function to check whether a table is empty
function is_empty(some_table)
    return next(some_table) == nil
end

function has_generic_tag(tags)
    for k, v in pairs(tags) do
        for j, k2 in ipairs(generic_keys) do
            if k == k2 then
                return true
            end
        end
    end
    return false
end

function osm2pgsql.process_node(object)
    if is_empty(object.tags) then
        return
    end

    for i,k in ipairs(delete_tags) do
        object.tags[k] = nil
    end

    if not has_generic_tag(object.tags) then
        return
    end

    tables.point:add_row(object.tags)
end

-- Treat objects with a key in polygon_keys as polygon
function is_polygon(tags)
    for i,k in ipairs(polygon_keys) do
        if tags[k] then
            return true
        end
    end
    return false
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    for i,k in ipairs(delete_tags) do
        object.tags[k] = nil
    end

    if not has_generic_tag(object.tags) then
        return
    end

    local polygon = is_polygon(object.tags)
    -- Treat objects tagged as area=yes, area=1, or area=true as polygon,
    -- and treat objects tagged as area=no, area=0, or area=false not as polygon
    local area_tag = object.tags.area
    if area_tag == 'yes' or area_tag == '1' or area_tag == 'true' then
        polygon = true
    elseif area_tag == 'no' or area_tag == '0' or area_tag == 'false' then
        polygon = false
    end

    local z_order, roads = get_z_order(object.tags)
    object.tags.z_order = z_order

    if polygon then
        tables.polygon:add_row(object.tags)
    else
        tables.line:add_row(object.tags)
    end

    if roads then
        tables.roads:add_row(object.tags)
    end
end

function osm2pgsql.process_relation(object)
    if is_empty(object.tags) then
        return
    end

    local type = object.tags.type
    if (type ~= 'route') and (type ~= 'multipolygon') and (type ~= 'boundary') then
        return
    end

    for i,k in ipairs(delete_tags) do
        object.tags[k] = nil
    end

    if not has_generic_tag(object.tags) then
        return
    end

    local z_order, roads = get_z_order(object.tags)
    object.tags.z_order = z_order

    local linestring = false
    if type == 'boundary' then
        linestring = true
    elseif type == 'multipolygon' then
        if object.tags.boundary then
            linestring = true
        else
            tables.polygon:add_row(object.tags)
        end
    end

    if linestring then
        tables.line:add_row(object.tags)
    end

    if roads then
        tables.roads:add_row(object.tags)
    end
end

