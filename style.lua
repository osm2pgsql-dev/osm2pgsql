-- For documentation of Lua tag transformations, see docs/lua.md.

-- Objects with any of the following keys will be treated as polygon
polygon_keys = {
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
generic_keys = {
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
delete_tags = {
    'FIXME',
    'note',
    'source'
}

-- Array used to specify z_order per key/value combination.
-- Each element has the form {key, value, z_order, is_road}.
-- If is_road=1, the object will be added to planet_osm_roads.
zordering_tags = {{ 'railway', nil, 5, 1}, { 'boundary', 'administrative', 0, 1},
    { 'bridge', 'yes', 10, 0 }, { 'bridge', 'true', 10, 0 }, { 'bridge', 1, 10, 0 },
    { 'tunnel', 'yes', -10, 0}, { 'tunnel', 'true', -10, 0}, { 'tunnel', 1, -10, 0},
    { 'highway', 'minor', 3, 0}, { 'highway', 'road', 3, 0 }, { 'highway', 'unclassified', 3, 0 },
    { 'highway', 'residential', 3, 0 }, { 'highway', 'tertiary_link', 4, 0}, { 'highway', 'tertiary', 4, 0},
    { 'highway', 'secondary_link', 6, 1}, { 'highway', 'secondary', 6, 1},
    { 'highway', 'primary_link', 7, 1}, { 'highway', 'primary', 7, 1},
    { 'highway', 'trunk_link', 8, 1}, { 'highway', 'trunk', 8, 1},
    { 'highway', 'motorway_link', 9, 1}, { 'highway', 'motorway', 9, 1},
}

function add_z_order(keyvalues)
    -- The default z_order is 0
    z_order = 0

    -- Add the value of the layer key times 10 to z_order
    if (keyvalues["layer"] ~= nil and tonumber(keyvalues["layer"])) then
       z_order = 10*keyvalues["layer"]
    end

   -- Increase or decrease z_order based on the specific key/value combination as specified in zordering_tags
    for i,k in ipairs(zordering_tags) do
        -- If the value in zordering_tags is specified, match key and value. Otherwise, match key only.
        if ((k[2]  and keyvalues[k[1]] == k[2]) or (k[2] == nil and keyvalues[k[1]] ~= nil)) then
            -- If the fourth component of the element of zordering_tags is 1, add the object to planet_osm_roads
            if (k[4] == 1) then
                roads = 1
            end
            z_order = z_order + k[3]
        end
    end

    -- Add z_order as key/value combination
    keyvalues["z_order"] = z_order

    return keyvalues, roads
end

-- Filtering on nodes, ways, and relations
function filter_tags_generic(keyvalues, numberofkeys)
    filter = 0   -- Will object be filtered out?

    -- Filter out objects with 0 tags
    if numberofkeys == 0 then
        filter = 1
        return filter, keyvalues
    end

    -- Delete tags listed in delete_tags
    for i,k in ipairs(delete_tags) do
        keyvalues[k] = nil
    end

    -- Filter out objects that do not have any of the keys in generic_keys
    tagcount = 0
    for k,v in pairs(keyvalues) do
        for i, k2 in ipairs(generic_keys) do
            if k2 == k then
                tagcount = tagcount + 1
            end
        end
    end
    if tagcount == 0 then
        filter = 1
    end

    return filter, keyvalues
end

-- Filtering on nodes
function filter_tags_node (keyvalues, numberofkeys)
    return filter_tags_generic(keyvalues, numberofkeys)
end

-- Filtering on relations
function filter_basic_tags_rel (keyvalues, numberofkeys)
    -- Filter out objects that are filtered out by filter_tags_generic
    filter, keyvalues = filter_tags_generic(keyvalues, numberofkeys)
    if filter == 1 then
        return filter, keyvalues
    end

    -- Filter out all relations except route, multipolygon and boundary relations
    if ((keyvalues["type"] ~= "route") and (keyvalues["type"] ~= "multipolygon") and (keyvalues["type"] ~= "boundary")) then
        filter = 1
        return filter, keyvalues
    end

    return filter, keyvalues
end

-- Filtering on ways
function filter_tags_way (keyvalues, numberofkeys)
    filter = 0  -- Will object be filtered out?
    polygon = 0 -- Will object be treated as polygon?
    roads = 0   -- Will object be added to planet_osm_roads?

    -- Filter out objects that are filtered out by filter_tags_generic
    filter, keyvalues = filter_tags_generic(keyvalues, numberofkeys)
    if filter == 1 then
        return filter, keyvalues, polygon, roads
    end

    -- Treat objects with a key in polygon_keys as polygon
    for i,k in ipairs(polygon_keys) do
        if keyvalues[k] then
            polygon=1
            break
        end
    end

    -- Treat objects tagged as area=yes, area=1, or area=true as polygon,
    -- and treat objects tagged as area=no, area=0, or area=false not as polygon
    if ((keyvalues["area"] == "yes") or (keyvalues["area"] == "1") or (keyvalues["area"] == "true")) then
        polygon = 1;
    elseif ((keyvalues["area"] == "no") or (keyvalues["area"] == "0") or (keyvalues["area"] == "false")) then
        polygon = 0;
    end

    -- Add z_order key/value combination and determine if the object should also be added to planet_osm_roads
    keyvalues, roads = add_z_order(keyvalues)

    return filter, keyvalues, polygon, roads
end

function filter_tags_relation_member (keyvalues, keyvaluemembers, roles, membercount)
    filter = 0     -- Will object be filtered out?
    linestring = 0 -- Will object be treated as linestring?
    polygon = 0    -- Will object be treated as polygon?
    roads = 0      -- Will object be added to planet_osm_roads?
    membersuperseded = {}
    for i = 1, membercount do
        membersuperseded[i] = 0 -- Will member be ignored when handling areas?
    end

    type = keyvalues["type"]

    -- Remove type key
    keyvalues["type"] = nil

    -- Relations with type=boundary are treated as linestring
    if (type == "boundary") then
        linestring = 1
    end
    -- Relations with type=multipolygon and boundary=* are treated as linestring
    if ((type == "multipolygon") and keyvalues["boundary"]) then
        linestring = 1
    -- For multipolygons...
    elseif (type == "multipolygon") then
        -- Treat as polygon
        polygon = 1
        polytagcount = 0;
        -- Count the number of polygon tags of the object
        for i,k in ipairs(polygon_keys) do
            if keyvalues[k] then
                polytagcount = 1
                break
            end
        end
        -- If there are no polygon tags, add tags from all outer elements to the multipolygon itself
        if (polytagcount == 0) then
            for i = 1,membercount do
                if (roles[i] == "outer") then
                    for k,v in pairs(keyvaluemembers[i]) do
                        keyvalues[k] = v
                    end
                end
            end

            f, keyvalues = filter_tags_generic(keyvalues, 1)
            -- check again if there are still polygon tags left
            polytagcount = 0
            for i,k in ipairs(polygon_keys) do
                if keyvalues[k] then
                    polytagcount = 1
                    break
                end
            end
            if polytagcount == 0 then
                filter = 1
            end
        end
        -- For any member of the multipolygon, set membersuperseded to 1 (i.e. don't deal with it as area as well),
        -- except when the member has a key/value combination such that
        --   1) the key occurs in generic_keys
        --   2) the key/value combination is not also a key/value combination of the multipolygon itself
        for i = 1,membercount do
            superseded = 1
            for k,v in pairs(keyvaluemembers[i]) do
                if ((keyvalues[k] == nil) or (keyvalues[k] ~= v)) then
                    for j,k2 in ipairs(generic_keys) do
                        if (k == k2) then
                            superseded = 0;
                            break
                        end
                    end
                end
            end
            membersuperseded[i] = superseded
        end
    end

    -- Add z_order key/value combination and determine if the object should also be added to planet_osm_roads
    keyvalues, roads = add_z_order(keyvalues)

    return filter, keyvalues, membersuperseded, linestring, polygon, roads
end
