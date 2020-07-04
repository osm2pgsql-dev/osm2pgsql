
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

local w2r = {}

function osm2pgsql.process_way(object)
    if osm2pgsql.stage == 1 then
        return
    end

    local row = {
        tags = object.tags,
        geom = { create = 'line' }
    }

    local d = w2r[object.id]
    if d then
        local refs = {}
        for rel_id, rel_ref in pairs(d) do
            refs[#refs + 1] = rel_ref
        end
        table.sort(refs)

        row.refs = table.concat(refs, ',')
    end

    tables.highways:add_row(row)
end

function osm2pgsql.select_relation_members(relation)
    if relation.tags.type == 'route' then
        return { ways = osm2pgsql.way_member_ids(relation) }
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.type ~= 'route' then
        return
    end

    local mlist = {}
    for _, member in ipairs(object.members) do
        if member.type == 'w' then
            if not w2r[member.ref] then
                w2r[member.ref] = {}
            end
            w2r[member.ref][object.id] = object.tags.ref
            mlist[#mlist + 1] = member.ref
        end
    end

    tables.routes:add_row({
        tags = object.tags,
        members = table.concat(mlist, ','),
        geom = { create = 'line' }
    })
end

