
local tables = {}

tables.t1 = osm2pgsql.define_node_table('osm2pgsql_test_t1', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point', not_null = true },
})

tables.t2 = osm2pgsql.define_node_table('osm2pgsql_test_t2', {
    { column = 'tags', type = 'hstore' },
    { column = 'rel_ids', type = 'text' },
    { column = 'geom', type = 'point', not_null = true },
})

tables.tboth = osm2pgsql.define_node_table('osm2pgsql_test_tboth', {
    { column = 'tags', type = 'hstore' },
    { column = 'rel_ids', type = 'text' },
    { column = 'geom', type = 'point', not_null = true },
})

local n2r = {}

local function get_ids(data)
    if data then
        local ids = {}
        for rel_id, _ in pairs(data) do
            ids[#ids + 1] = rel_id
        end
        table.sort(ids)
        return '{' .. table.concat(ids, ',') .. '}'
    end
end

function osm2pgsql.process_node(object)
    if object.tags.t1 then
        tables.t1:insert{
            tags = object.tags,
            geom = object:as_point()
        }
    end

    if osm2pgsql.stage == 2 and object.tags.t2 then
        local ids = get_ids(n2r[object.id])
        if ids then
            tables.t2:insert{
                rel_ids = ids,
                geom = object:as_point()
            }
        end
    end

    if object.tags.tboth then
        local ids = get_ids(n2r[object.id])
        tables.tboth:insert{
            tags = object.tags,
            rel_ids = ids,
            geom = object:as_point()
        }
    end
end

local function node_member_ids(relation)
    local ids = {}
    for _, member in ipairs(relation.members) do
        if member.type == 'n' and member.role == 'mark' then
            ids[#ids + 1] = member.ref
        end
    end
    return ids
end

function osm2pgsql.select_relation_members(relation)
    return { nodes = node_member_ids(relation) }
end

function osm2pgsql.process_relation(object)
    for _, member in ipairs(object.members) do
        if member.type == 'n' and member.role == 'mark' then
            if not n2r[member.ref] then
                n2r[member.ref] = {}
            end
            n2r[member.ref][object.id] = true
        end
    end
end

