
tables = {}

tables.highways = osm2pgsql.define_table{
    name = 'osm2pgsql_test_highways',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'tags',  type = 'hstore' },
        { column = 'refs',  type = 'text' },
        { column = 'min_x', type = 'real' },
        { column = 'min_y', type = 'real' },
        { column = 'max_x', type = 'real' },
        { column = 'max_y', type = 'real' },
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

if osm2pgsql.stage == 1 then
    osm2pgsql.userdata.by_way_id = {}
end

function osm2pgsql.process_way(data)
    if osm2pgsql.stage == 1 then
        osm2pgsql.mark('w', data.id)
        return
    end

    local row = {
        tags = data.tags,
    }

    row.min_x, row.min_y, row.max_x, row.max_y = osm2pgsql.get_bbox()

    -- if there is any data from relations, add it in
    local d = osm2pgsql.userdata.by_way_id[data.id]
    if d then
        row.refs = table.concat(d.refs, ',')
    --    row.rel_ids = '{' .. table.concat(d.ids, ',') .. '}'
    end

    tables.highways:add_row(row)
end

function osm2pgsql.process_relation(data)
    if data.tags.type ~= 'route' then
        return
    end

    if not osm2pgsql.userdata.by_way_id then
        osm2pgsql.userdata.by_way_id = {}
    end

    local mlist = {}
    for i, member in ipairs(data.members) do
        if member.type == 'w' then
            osm2pgsql.mark('w', member.ref)
            if not osm2pgsql.userdata.by_way_id[member.ref] then
                osm2pgsql.userdata.by_way_id[member.ref] = {
                    ids = {},
                    refs = {}
                }
            end
            local d = osm2pgsql.userdata.by_way_id[member.ref]
            table.insert(d.ids, data.id)
            table.insert(d.refs, data.tags.ref)
            mlist[#mlist + 1] = member.ref
        end
    end

    tables.routes:add_row({
        tags = data.tags,
        members = table.concat(mlist, ',')
    })
end

