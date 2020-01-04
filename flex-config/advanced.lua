
-- Read and understand simple.lua and multipolygons.lua first, before you try
-- to understand this file.

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

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring' },
})

-- Using the define_table function allows some more control over the id columns
-- than the more convenient define_(node|way|relation|area)_table functions.
-- In this case we are setting the name of the id column to "osm_id".
tables.polygons = osm2pgsql.define_table{
    name = 'polygons',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
    }
}

-- A table for all route relations
tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'multilinestring' },
})

-- A table for all individual members of route relations
-- (Note that this script doesn't handle ways in multiple relations correctly.)
tables.route_members = osm2pgsql.define_table{
    name = 'route_members',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'rel_id', type = 'int8' }, -- not a specially handled id column
        { column = 'tags',   type = 'hstore' }, -- tags from member way
        { column = 'role',   type = 'text' }, -- role in the relation
        { column = 'rtags',  type = 'hstore' }, -- tags from relation
        { column = 'geom',   type = 'linestring' },
    }
}

if osm2pgsql.stage == 1 then
    osm2pgsql.userdata.route_tags = {}
    osm2pgsql.userdata.w2r = {}
end

function is_empty(some_table)
    return next(some_table) == nil
end

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
end

function osm2pgsql.process_node(object)
    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    tables.pois:add_row({
        tags = object.tags
    })
end

function osm2pgsql.process_way(object)
--    print(inspect(object))

    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    -- osm2pgsql.stage: either 1 or 2 for first or second pass through the data
    if osm2pgsql.stage == 2 then
        local row = {
            rel_id = 0,
            tags = object.tags,
            role = '',
            rtags = {},
        }
        member_data = osm2pgsql.userdata.w2r[object.id]
        if member_data then
            row.rel_id = member_data.rel_id
            row.role = member_data.role
            row.rtags = osm2pgsql.userdata.route_tags[row.rel_id]
        end
        -- print(inspect(row))
        tables.route_members:add_row(row)
        return
    end

    if object.is_closed then
         tables.polygons:add_row({
            tags = object.tags
        })
    else
         tables.ways:add_row({
            tags = object.tags
        })
    end
end

function osm2pgsql.process_relation(object)
--    print(inspect(object))

    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    if object.tags.type == 'multipolygon' or object.tags.type == 'boundary' then
         tables.polygons:add_row({
            tags = object.tags
        })
    elseif object.tags.type == 'route' and object.tags.route == 'hiking' then
        tables.routes:add_row({
            tags = object.tags
        })

        osm2pgsql.userdata.route_tags[object.id] = object.tags

        -- Go through all the members...
        for i, member in ipairs(object.members) do
            if member.type == 'w' then
                -- Mark the member way as "interesting", the "process_way"
                -- callback will be triggered again in the second stage
                osm2pgsql.mark('w', member.ref)
                -- print("mark way id " .. member.ref)
                osm2pgsql.userdata.w2r[member.ref] = {
                    rel_id = object.id,
                    role = member.role,
                }
            end
        end
    end
end

