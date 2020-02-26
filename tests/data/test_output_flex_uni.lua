
local test_table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_data',
    ids = { type = 'any', type_column = 'osm_type', id_column = 'osm_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'name', type = 'text' },
        { column = 'geom', type = 'geometry' },
    }
}

function is_empty(some_table)
    return next(some_table) == nil
end

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil
    tags['source:name'] = nil
end

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

function osm2pgsql.process_node(data)
    clean_tags(data.tags)
    if is_empty(data.tags) then
        return
    end

    test_table:add_row({
        tags = data.tags,
        geom = { create = 'point' }
    })
end

function osm2pgsql.process_way(data)
    clean_tags(data.tags)
    if is_empty(data.tags) then
        return
    end

    if is_polygon(data.tags) then
        test_table:add_row({
            tags = data.tags,
            name = data.tags.name,
            geom = { create = 'area' }
        })
    else
        test_table:add_row({
            tags = data.tags,
            name = data.tags.name,
            geom = { create = 'line' }
        })
    end
end

function osm2pgsql.process_relation(data)
    clean_tags(data.tags)
    if is_empty(data.tags) then
        return
    end

    if data.tags.type == 'multipolygon' or data.tags.type == 'boundary' then
        test_table:add_row({
            tags = data.tags,
            name = data.tags.name,
            geom = { create = 'area', multi = false }
        })
        return
    end

    if data.tags.type == 'route' then
        test_table:add_row({
            tags = data.tags,
            name = data.tags.name,
            geom = { create = 'line' }
        })
    end
end

