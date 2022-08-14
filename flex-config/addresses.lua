-- This config example file is released into the Public Domain.

-- Get a location for all objects with addresses. If mapped as a polygon or
-- multipolygon, the centroid will be used.

local addrs = osm2pgsql.define_table({
    name = 'addrs',
    ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
    columns = {
        { column = 'name', type = 'text' },
        { column = 'country', type = 'text' },
        { column = 'state', type = 'text' },
        { column = 'postcode', type = 'text' },
        { column = 'city', type = 'text' },
        { column = 'place', type = 'text' },
        { column = 'street', type = 'text' },
        { column = 'housenumber', type = 'text' },
        { column = 'geom', type = 'point', projection = 4326, not_null = true }
    }
})

function get_address(tags)
    local addr = {}
    local count = 0

    for _, key in ipairs({'housenumber', 'street', 'city', 'postcode', 'country', 'state', 'place'}) do
        local a = tags['addr:' .. key]
        if a then
            addr[key] = a
            count = count + 1
        end
    end

    return count > 1, addr
end

function osm2pgsql.process_node(object)
    local any, addr = get_address(object.tags)
    if any then
        addr.name = object.tags.name
        addr.geom = object:as_point()
        addrs:insert(addr)
    end
end

function osm2pgsql.process_way(object)
    if object.is_closed then
        local any, addr = get_address(object.tags)
        if any then
            addr.name = object.tags.name
            addr.geom = object:as_polygon():centroid()
            addrs:insert(addr)
        end
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.type == 'multipolygon' then
        local any, addr = get_address(object.tags)
        if any then
            addr.name = object.tags.name
            addr.geom = object:as_multipolygon():centroid()
            addrs:insert(addr)
        end
    end
end

