
local tables = {}

tables.point = osm2pgsql.define_node_table('osm2pgsql_test_point', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
})

tables.line = osm2pgsql.define_table{
    name = 'osm2pgsql_test_line',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'name', type = 'text' },
        { column = 'geom', type = 'linestring' },
    }
}

tables.polygon = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'name', type = 'text' },
        { column = 'geom', type = 'geometry' },
        { column = 'area', type = 'area' },
    }
}

tables.route = osm2pgsql.define_table{
    name = 'osm2pgsql_test_route',
    ids = { type = 'relation', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'multilinestring' },
    }
}

function is_polygon(tags)
    if tags.aeroway
        or tags.amenity
        or tags.area
        or tags.building
        or tags.harbour
        or tags.historic
        or tags.landuse
        or tags.leisure
        or tags.man_made
        or tags.military
        or tags.natural
        or tags.office
        or tags.place
        or tags.power
        or tags.public_transport
        or tags.shop
        or tags.sport
        or tags.tourism
        or tags.water
        or tags.waterway
        or tags.wetland
        or tags['abandoned:aeroway']
        or tags['abandoned:amenity']
        or tags['abandoned:building']
        or tags['abandoned:landuse']
        or tags['abandoned:power']
        or tags['area:highway']
        then
        return true
    else
        return false
    end
end

local delete_keys = {
    'odbl',
    'created_by',
    'source'
}

local clean_tags = osm2pgsql.make_clean_tags_func(delete_keys)

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    tables.point:add_row({
        tags = object.tags
    })
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    if is_polygon(object.tags) then
        tables.polygon:add_row({
            tags = object.tags,
            name = object.tags.name,
            geom = { create = 'area' }
        })
    else
        tables.line:add_row({
            tags = object.tags,
            name = object.tags.name
        })
    end
end

function osm2pgsql.process_relation(object)
    if clean_tags(object.tags) then
        return
    end

    if object.tags.type == 'multipolygon' or object.tags.type == 'boundary' then
        tables.polygon:add_row({
            tags = object.tags,
            name = object.tags.name,
            geom = { create = 'area', multi = false }
        })
        return
    end

    if object.tags.type == 'route' then
        tables.route:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })
    end
end

