
local tables = {}

tables.highways = osm2pgsql.define_table{
    name = 'osm2pgsql_test_highways',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'tags',  type = 'hstore' },
        { column = 'refs',  type = 'text' },
        { column = 'geom',  type = 'linestring' },
    }
}

tables.routes = osm2pgsql.define_table{
    name = 'osm2pgsql_test_routes',
    ids = { type = 'relation', id_column = 'rel_id' },
    columns = {
        { column = 'tags',    type = 'hstore' },
        { column = 'members', type = 'text' },
        { column = 'geom',    type = 'multilinestring' },
    }
}

local by_way_id = {}

function osm2pgsql.process_way(object)
    if osm2pgsql.stage == 1 then
        osm2pgsql.mark_way(object.id)
        return
    end

    local row = {
        tags = object.tags,
        geom = { create = 'line' }
    }

    -- if there is any data from relations, add it in
    local d = by_way_id[object.id]
    if d then
        local keys = {}
        for k,v in pairs(d.refs) do
            keys[#keys + 1] = k
        end

        row.refs = table.concat(keys, ',')
    --    row.rel_ids = '{' .. table.concat(d.ids, ',') .. '}'
    end

    tables.highways:add_row(row)
end

function osm2pgsql.process_relation(object)
    if object.tags.type ~= 'route' then
        return
    end

    local mlist = {}
    for i, member in ipairs(object.members) do
        if member.type == 'w' then
            osm2pgsql.mark_way(member.ref)
            if not by_way_id[member.ref] then
                by_way_id[member.ref] = {
                    ids = {},
                    refs = {}
                }
            end
            local d = by_way_id[member.ref]
            table.insert(d.ids, object.id)
            d.refs[object.tags.ref] = 1
            mlist[#mlist + 1] = member.ref
        end
    end

    tables.routes:add_row({
        tags = object.tags,
        members = table.concat(mlist, ','),
        geom = { create = 'line' }
    })
end

