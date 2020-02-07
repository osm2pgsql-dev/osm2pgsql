
-- This file shows how to use multi-stage processing to bring tags from
-- relations into ways.

-- This will only import ways tagged as highway. The 'rel_refs' text column
-- will contain a comma-separated list of all ref tags found in parent
-- relations with type=route and route=road. The 'rel_ids' column will be
-- an integer array containing the relation ids. These could be used, for
-- instance, to look up other relation tags from the 'routes' table.

local tables = {}

tables.highways = osm2pgsql.define_way_table('highways', {
    { column = 'tags',     type = 'hstore' },
    { column = 'rel_refs', type = 'text' }, -- for the refs from the relations
    { column = 'rel_ids',  type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',     type = 'linestring' },
})

-- Tables don't have to have a geometry column
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'hstore' },
})

-- This will be used to store lists of relation ids queryable by way id
by_way_id = {}

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

    -- In stage 1: Mark all remaining ways so we will see them again in stage 2
    if osm2pgsql.stage == 1 then
        osm2pgsql.mark_way(object.id)
        return
    end

    -- We are now in stage 2

    clean_tags(object.tags)

    -- Data we will store in the "highways" table always has the way tags
    local row = {
        tags = object.tags
    }

    -- If there is any data from relations, add it in
    local d = by_way_id[object.id]
    if d then
        table.sort(d.refs)
        table.sort(d.ids)
        row.rel_refs = table.concat(d.refs, ',')
        row.rel_ids = '{' .. table.concat(d.ids, ',') .. '}'
    end

    tables.highways:add_row(row)
end

function osm2pgsql.process_relation(object)
    -- Only interested in relations with type=route, route=road and a ref
    if object.tags.type == 'route' and object.tags.route == 'road' and object.tags.ref then
        tables.routes:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })

        -- Go through all the members and store relation ids and refs so it
        -- can be found by the way id.
        for _, member in ipairs(object.members) do
            if member.type == 'w' then
                if not by_way_id[member.ref] then
                    by_way_id[member.ref] = {
                        ids = {},
                        refs = {}
                    }
                end
                local d = by_way_id[member.ref]
                table.insert(d.ids, object.id)
                table.insert(d.refs, object.tags.ref)
            end
        end
    end
end

