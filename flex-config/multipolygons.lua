
inspect = require('inspect')

print('osm2pgsql version: ' .. osm2pgsql.version)

-- The projection configured on the command line is available in Lua:
print('osm2pgsql srid: ' .. osm2pgsql.srid)

local tables = {}

tables.pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' },
})

tables.ways = osm2pgsql.define_way_table('ways', {
    { column = 'tags', type = 'hstore' },
    -- If you want to split long linestrings, you can set "split_at" to the
    -- maximum length the pieces should have. This length is in map units,
    -- so it depends on the projection used. "Traditional" osm2pgsql sets
    -- this to 1 for 4326 geometries and 100000 for 3857 (web mercator)
    -- geometries. The default is 0.0, which means no splitting.
    { column = 'geom', type = 'linestring', split_at = 100000 },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'geometry' },
    -- The 'area' type is used to store the calculated area of a polygon feature
    { column = 'area', type = 'area' },
})

function is_empty(some_table)
    return next(some_table) == nil
end

function starts_with(str, start)
   return str:sub(1, #start) == start
end

function remove_with_prefix(tags, prefix)
    for k, v in pairs(tags) do
        if starts_with(k, prefix) then
            tags.k = nil
        end
    end
end

function clean_tags(tags)
    -- These are tags that are generally regarded as useless for most
    -- rendering. Most of them are from imports or intended as internal
    -- information for mappers Some of them are automatically deleted by
    -- editors. If you want some of them, perhaps for a debugging layer, just
    -- delete the lines.

    -- These tags are used by mappers to keep track of data.
    -- They aren't very useful for rendering.
    tags['note'] = nil
    remove_with_prefix(tags, 'note:')
    tags['source'] = nil
    tags['source_ref'] = nil
    remove_with_prefix(tags, 'source:')
    tags['attribution'] = nil
    tags['comment'] = nil
    tags['fixme'] = nil

    -- Tags generally dropped by editors, not otherwise covered
    tags['created_by'] = nil
    tags['odbl'] = nil
    tags['odbl:note'] = nil
    tags['SK53_bulk:load'] = nil

    -- Lots of import tags
    -- TIGER (US)
    remove_with_prefix(tags, 'tiger:')

    -- NHD (US)
    -- NHD has been converted every way imaginable
    remove_with_prefix(tags, 'NHD:')
    remove_with_prefix(tags, 'nhd:')

    -- GNIS (US)
    remove_with_prefix(tags, 'gnis:')

    -- Geobase (CA)
    remove_with_prefix(tags, 'geobase:')
    -- NHN (CA)
    tags['accuracy:meters'] = nil
    tags['sub_sea:type'] = nil
    tags['waterway:type'] = nil

    -- KSJ2 (JA)
    -- See also note:ja and source_ref above
    remove_with_prefix(tags, 'KSJ2:')
    -- Yahoo/ALPS (JA)
    remove_with_prefix(tags, 'yh:')

    -- osak (DK)
    remove_with_prefix(tags, 'osak:')

    -- kms (DK)
    remove_with_prefix(tags, 'kms:')

    -- ngbe (ES)
    -- See also note:es and source:file above
    remove_with_prefix(tags, 'ngbe:')

    -- naptan (UK)
    remove_with_prefix(tags, 'naptan:')

    -- Corine (CLC) (Europe)
    remove_with_prefix(tags, 'CLC:')

    -- misc
    tags['3dshapes:ggmodelk'] = nil
    tags['AND_nosr_r'] = nil
    tags['import'] = nil
    remove_with_prefix(tags, 'it:fvg:')
end

-- Helper function that looks at the tags and decides if this is possibly
-- an area.
function has_area_tags(tags)
    if tags.area == 'yes' then
        return true
    end
    if tags.area == 'no' then
        return false
    end

    return tags.aeroway
        or tags.amenity
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
    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    -- A closed way that also has the right tags for an area is a polygon.
    if object.is_closed and has_area_tags(object.tags) then
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
    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    -- Only handle multipolygons
    if object.tags.type == 'multipolygon' or object.tags.type == 'boundary' then
         tables.polygons:add_row({
            tags = object.tags
        })
    end
end

