-- This config example file is released into the Public Domain.

-- This file shows how to use multi-stage processing to bring tags from
-- public transport relations into member nodes and ways. This allows
-- advanced processing of public transport networks including stops.

-- Nodes tagged as public transport stops are imported into the 'stops' table,
-- if they are part of a public transport relation. Ways tagged as highway or
-- railway or imported into the 'lines' table. The public transport routes
-- themselves will be in the 'routes' table, but without any geometry. As a
-- "bonus" public transport stop area relations will be imported into the
-- 'stop_areas' table.
--
-- For the 'stops' and 'lines' table two-stage processing is used. The
-- 'rel_refs' text column will contain a list of all ref tags found in parent
-- relations with type=route and route=public_transport. The 'rel_ids' column
-- will be an integer array containing the relation ids. These could be used,
-- for instance, to look up other relation tags from the 'routes' table.

local tables = {}

tables.stops = osm2pgsql.define_node_table('stops', {
    { column = 'tags',     type = 'jsonb' },
    { column = 'rel_refs', type = 'text' }, -- for the refs from the relations
    { column = 'rel_ids',  sql_type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',     type = 'point', not_null = true },
})

tables.lines = osm2pgsql.define_way_table('lines', {
    { column = 'tags',     type = 'jsonb' },
    { column = 'rel_refs', type = 'text' }, -- for the refs from the relations
    { column = 'rel_ids',  sql_type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',     type = 'linestring', not_null = true },
})

-- Tables don't have to have a geometry column
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'ref',  type = 'text' },
    { column = 'type', type = 'text' },
    { column = 'from', type = 'text' },
    { column = 'to',   type = 'text' },
    { column = 'tags', type = 'jsonb' },
})

-- Stop areas contain everything belonging to a specific public transport
-- stop. We model them here by adding a center point as geometry plus the
-- radius of a circle that contains everything in that stop.
tables.stop_areas = osm2pgsql.define_relation_table('stop_areas', {
    { column = 'tags',   type = 'jsonb' },
    { column = 'radius', type = 'real', not_null = true },
    { column = 'geom',   type = 'point', not_null = true },
})

-- This will be used to store information about relations queryable by member
-- node/way id. These are table of tables. The outer table is indexed by the
-- node/way id, the inner table indexed by the relation id. This way even if
-- the information about a relation is added twice, it will be in there only
-- once. It is always good to write your osm2pgsql Lua code in an idempotent
-- way, i.e. it can be called any number of times and will lead to the same
-- result.
local n2r = {}
local w2r = {}

local function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

local function unique_array(array)
    local result = {}

    local last = nil
    for _, v in ipairs(array) do
        if v ~= last then
            result[#result + 1] = v
            last = v
        end
    end

    return result
end

local separator = '·' -- use middle dot as separator character

local function add_rel_data(row, d)
    if not d then
        return
    end

    local refs = {}
    local ids = {}
    for rel_id, rel_ref in pairs(d) do
        refs[#refs + 1] = rel_ref
        ids[#ids + 1] = rel_id
    end
    table.sort(refs)
    table.sort(ids)

    row.rel_refs = table.concat(unique_array(refs), separator)
    row.rel_ids = '{' .. table.concat(unique_array(ids), ',') .. '}'
end

function osm2pgsql.process_node(object)
    -- We are only interested in public transport stops here, and they are
    -- only available in the second stage.
    if osm2pgsql.stage ~= 2 then
        return
    end

    local row = {
        tags = object.tags,
        geom = object:as_point()
    }

    -- If there is any data from parent relations, add it in
    add_rel_data(row, n2r[object.id])

    tables.stops:insert(row)
end

function osm2pgsql.process_way(object)
    -- We are only interested in highways and railways
    if not object.tags.highway and not object.tags.railway then
        return
    end

    clean_tags(object.tags)

    -- Data we will store in the 'lines' table always has the tags from
    -- the way
    local row = {
        tags = object.tags,
        geom = object:as_linestring()
    }

    -- If there is any data from parent relations, add it in
    add_rel_data(row, w2r[object.id])

    tables.lines:insert(row)
end

local pt = {
    bus = true,
    light_rail = true,
    subway = true,
    tram = true,
    trolleybus = true,
}

-- We are only interested in certain route relations with a ref tag
local function wanted_relation(tags)
    return tags.type == 'route' and pt[tags.route] and tags.ref
end

-- This function is called for every added, modified, or deleted relation.
-- Its only job is to return the ids of all member nodes/ways of the specified
-- relation we want to see in stage 2 again. It MUST NOT store any information
-- about the relation!
function osm2pgsql.select_relation_members(relation)
    -- Only interested in public transport relations with refs
    if wanted_relation(relation.tags) then
        local node_ids = {}
        local way_ids = {}

        for _, member in ipairs(relation.members) do
            if member.type == 'n' and member.role == 'stop' then
                node_ids[#node_ids + 1] = member.ref
            elseif member.type == 'w' and member.role == '' then
                way_ids[#way_ids + 1] = member.ref
            end
        end

        return {
            nodes = node_ids,
            ways = way_ids,
        }
    end
end

-- The process_relation() function should store all information about relation
-- members that might be needed in stage 2.
function osm2pgsql.process_relation(object)
    if object.tags.type == 'public_transport' and object.tags.public_transport == 'stop_area' then
        local x1, y1, x2, y2 = object:as_geometrycollection():transform(3857):get_bbox()
        local radius = math.sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1))
        tables.stop_areas:insert({
            tags = object.tags,
            geom = object:as_geometrycollection():centroid(),
            radius = radius,
        })
        return
    end

    if wanted_relation(object.tags) then
        tables.routes:insert({
            type = object.tags.route,
            ref = object.tags.ref,
            from = object.tags.from,
            to = object.tags.to,
            tags = object.tags,
        })

        -- Go through all the members and store relation ids and refs so they
        -- can be found by the member node/way id.
        for _, member in ipairs(object.members) do
            if member.type == 'n' then
                if not n2r[member.ref] then
                    n2r[member.ref] = {}
                end
                n2r[member.ref][object.id] = object.tags.ref
            elseif member.type == 'w' then
                if not w2r[member.ref] then
                    w2r[member.ref] = {}
                end
                w2r[member.ref][object.id] = object.tags.ref
            end
        end
    end
end

