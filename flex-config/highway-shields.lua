
-- Read and understand simple.lua and multipolygons.lua first, before you try
-- to understand this file.

-- This will import highways only. The 'refs' column will contain a
-- comma-separated list of all refs found in relations with type=route and
-- route=road.

inspect = require('inspect')

print('osm2pgsql version: ' .. osm2pgsql.version)

-- Are we running in "create" or "append" mode?
print('osm2pgsql mode: ' .. osm2pgsql.mode)

-- Which stage in the data processing is this?
print('osm2pgsql stage: ' .. osm2pgsql.stage)

-- Uncomment the following line to see the userdata (but, careful, it might be
--                                                   a lot of data)
-- print('osm2pgsql userdata: ' .. inspect(osm2pgsql.userdata))

local tables = {}

tables.highways = osm2pgsql.define_way_table('highways', {
    { column = 'tags',    type = 'hstore' },
    { column = 'refs',    type = 'text' },
    { column = 'oneway',  type = 'direction' }, -- the 'direction' type maps "yes", "true", and "1" to 1, "-1" to -1, everything else to 0
    { column = 'rel_ids', type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',    type = 'linestring' },
})

-- tables don't have to have a geometry column
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'hstore' },
})

if osm2pgsql.stage == 1 then
    osm2pgsql.userdata.by_way_id = {}
end

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
end

function osm2pgsql.process_way(object)
    -- we are only interested in highways
    if not object.tags.highway then
        return
    end

    -- mark all remaining ways so we will see them again in the second stage
    if osm2pgsql.stage == 1 then
        osm2pgsql.mark('w', object.id)
        return
    end

    -- we are now in second stage

    clean_tags(object.tags)

    local row = {
        tags = object.tags,
        oneway = object.tags.oneway or 0,
    }

    -- if there is any data from relations, add it in
    local d = osm2pgsql.userdata.by_way_id[object.id]
    if d then
        row.refs = table.concat(d.refs, ',')
        row.rel_ids = '{' .. table.concat(d.ids, ',') .. '}'
    end

    -- print(inspect(row))

    tables.highways:add_row(row)
end

function osm2pgsql.process_relation(object)
    -- only interested in relations with type=route, route=road and a ref
    if object.tags.type == 'route' and object.tags.route == 'road' and object.tags.ref then
        tables.routes:add_row({
            tags = object.tags
        })

        -- Go through all the members and store relation ids and refs so it
        -- can be found by the way id.
        for i, member in ipairs(object.members) do
            if member.type == 'w' then
                if not osm2pgsql.userdata.by_way_id[member.ref] then
                    osm2pgsql.userdata.by_way_id[member.ref] = {
                        ids = {},
                        refs = {}
                    }
                end
                local d = osm2pgsql.userdata.by_way_id[member.ref]
                table.insert(d.ids, object.id)
                table.insert(d.refs, object.tags.ref)
            end
        end
    end
end

