-- This config example file is released into the Public Domain.

-- This file shows how to use multi-stage processing to bring tags from
-- relations into ways.

-- This will only import ways tagged as highway. The 'rel_refs' text column
-- will contain a comma-separated list of all ref tags found in parent
-- relations with type=route and route=road. The 'rel_ids' column will be
-- an integer array containing the relation ids. These could be used, for
-- instance, to look up other relation tags from the 'routes' table.

local tables = {}

tables.highways = osm2pgsql.define_way_table('highways', {
    { column = 'tags',     type = 'jsonb' },
    { column = 'rel_refs', type = 'text' }, -- for the refs from the relations
    { column = 'rel_ids',  sql_type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',     type = 'linestring', not_null = true },
})

-- Tables don't have to have a geometry column
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'jsonb' },
})

-- This will be used to store information about relations queryable by member
-- way id. It is a table of tables. The outer table is indexed by the way id,
-- the inner table indexed by the relation id. This way even if the information
-- about a relation is added twice, it will be in there only once. It is
-- always good to write your osm2pgsql Lua code in an idempotent way, i.e.
-- it can be called any number of times and will lead to the same result.
local w2r = {}

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

function osm2pgsql.process_way(object)
    -- We are only interested in highways
    if not object.tags.highway then
        return
    end

    clean_tags(object.tags)

    -- Data we will store in the "highways" table always has the tags from
    -- the way
    local row = {
        tags = object.tags,
        geom = object:as_linestring()
    }

    -- If there is any data from parent relations, add it in
    local d = w2r[object.id]
    if d then
        local refs = {}
        local ids = {}
        for rel_id, rel_ref in pairs(d) do
            refs[#refs + 1] = rel_ref
            ids[#ids + 1] = rel_id
        end
        table.sort(refs)
        table.sort(ids)
        row.rel_refs = table.concat(refs, ',')
        row.rel_ids = '{' .. table.concat(ids, ',') .. '}'
    end

    tables.highways:insert(row)
end

-- This function is called for every added, modified, or deleted relation.
-- Its only job is to return the ids of all member ways of the specified
-- relation we want to see in stage 2 again. It MUST NOT store any information
-- about the relation!
function osm2pgsql.select_relation_members(relation)
    -- Only interested in relations with type=route, route=road and a ref
    if relation.tags.type == 'route' and relation.tags.route == 'road' and relation.tags.ref then
        return { ways = osm2pgsql.way_member_ids(relation) }
    end
end

-- The process_relation() function should store all information about way
-- members that might be needed in stage 2.
function osm2pgsql.process_relation(object)
    if object.tags.type == 'route' and object.tags.route == 'road' and object.tags.ref then
        tables.routes:insert({
            tags = object.tags
        })

        -- Go through all the members and store relation ids and refs so they
        -- can be found by the way id.
        for _, member in ipairs(object.members) do
            if member.type == 'w' then
                if not w2r[member.ref] then
                    w2r[member.ref] = {}
                end
                w2r[member.ref][object.id] = object.tags.ref
            end
        end
    end
end

