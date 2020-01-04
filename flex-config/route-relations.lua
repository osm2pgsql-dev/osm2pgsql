
--inspect = require('inspect')

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
    { column = 'rel_ids', type = 'int8[]' }, -- array with integers (for relation IDs)
    { column = 'geom',    type = 'linestring' },
})

-- tables don't have to have a geometry column
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'hstore' },
})

if osm2pgsql.stage == 1 then
    osm2pgsql.userdata.tags_by_rel_id = {}
    osm2pgsql.userdata.rels_by_way_id = {}
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

    local row = { tags = object.tags }

    local rel_ids = osm2pgsql.userdata.rels_by_way_id[object.id]
    if rel_ids then
        local refs = {}
        for i, rel_id in ipairs(rel_ids) do
            local rel_tags = osm2pgsql.userdata.tags_by_rel_id[rel_id]
            if rel_tags.ref then
                table.insert(refs, rel_tags.ref)
            end
        end

        row.refs = table.concat(refs, ',')
        row.rel_ids = '{' .. table.concat(rel_ids, ',') .. '}'
    end

    -- print(inspect(row))

    tables.highways:add_row(row)
end

function osm2pgsql.process_relation(object)
    -- only interested in relations with type=route
    if object.tags.type ~= 'route' then
        return
    end

    tables.routes:add_row({
        tags = object.tags
    })

    -- Store all tags of this relation
    osm2pgsql.userdata.tags_by_rel_id[object.id] = object.tags

    -- Create index to find relation ids by their way member ids
    for i, member in ipairs(object.members) do
        if member.type == 'w' then
            if not osm2pgsql.userdata.rels_by_way_id[member.ref] then
                osm2pgsql.userdata.rels_by_way_id[member.ref] = {}
            end
            table.insert(osm2pgsql.userdata.rels_by_way_id[member.ref], object.id)
        end
    end
end

