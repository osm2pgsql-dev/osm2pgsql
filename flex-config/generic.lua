-- This config example file is released into the Public Domain.

-- This is a generic configuration that is a good starting point for
-- real-world projects. Data is split into tables according to geometry type
-- and most tags are stored in jsonb columns.

-- Set this to the projection you want to use
local srid = 3857

local tables = {}

tables.points = osm2pgsql.define_node_table('points', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'point', projection = srid },
})

tables.lines = osm2pgsql.define_way_table('lines', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'linestring', projection = srid },
})

tables.polygons = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'geometry', projection = srid },
    { column = 'area', type = 'area' },
})

tables.routes = osm2pgsql.define_relation_table('routes', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'multilinestring', projection = srid },
})

tables.boundaries = osm2pgsql.define_relation_table('boundaries', {
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'multilinestring', projection = srid },
})

-- These tag keys are generally regarded as useless for most rendering. Most
-- of them are from imports or intended as internal information for mappers.
--
-- If a key ends in '*' it will match all keys with the specified prefix.
--
-- If you want some of these keys, perhaps for a debugging layer, just
-- delete the corresponding lines.
local delete_keys = {
    -- "mapper" keys
    'attribution',
    'comment',
    'created_by',
    'fixme',
    'note',
    'note:*',
    'odbl',
    'odbl:note',
    'source',
    'source:*',
    'source_ref',

    -- "import" keys

    -- Corine Land Cover (CLC) (Europe)
    'CLC:*',

    -- Geobase (CA)
    'geobase:*',
    -- CanVec (CA)
    'canvec:*',

    -- osak (DK)
    'osak:*',
    -- kms (DK)
    'kms:*',

    -- ngbe (ES)
    -- See also note:es and source:file above
    'ngbe:*',

    -- Friuli Venezia Giulia (IT)
    'it:fvg:*',

    -- KSJ2 (JA)
    -- See also note:ja and source_ref above
    'KSJ2:*',
    -- Yahoo/ALPS (JA)
    'yh:*',

    -- LINZ (NZ)
    'LINZ2OSM:*',
    'linz2osm:*',
    'LINZ:*',
    'ref:linz:*',

    -- WroclawGIS (PL)
    'WroclawGIS:*',
    -- Naptan (UK)
    'naptan:*',

    -- TIGER (US)
    'tiger:*',
    -- GNIS (US)
    'gnis:*',
    -- National Hydrography Dataset (US)
    'NHD:*',
    'nhd:*',
    -- mvdgis (Montevideo, UY)
    'mvdgis:*',

    -- EUROSHA (Various countries)
    'project:eurosha_2012',

    -- UrbIS (Brussels, BE)
    'ref:UrbIS',

    -- NHN (CA)
    'accuracy:meters',
    'sub_sea:type',
    'waterway:type',
    -- StatsCan (CA)
    'statscan:rbuid',

    -- RUIAN (CZ)
    'ref:ruian:addr',
    'ref:ruian',
    'building:ruian:type',
    -- DIBAVOD (CZ)
    'dibavod:id',
    -- UIR-ADR (CZ)
    'uir_adr:ADRESA_KOD',

    -- GST (DK)
    'gst:feat_id',

    -- Maa-amet (EE)
    'maaamet:ETAK',
    -- FANTOIR (FR)
    'ref:FR:FANTOIR',

    -- 3dshapes (NL)
    '3dshapes:ggmodelk',
    -- AND (NL)
    'AND_nosr_r',

    -- OPPDATERIN (NO)
    'OPPDATERIN',
    -- Various imports (PL)
    'addr:city:simc',
    'addr:street:sym_ul',
    'building:usage:pl',
    'building:use:pl',
    -- TERYT (PL)
    'teryt:simc',

    -- RABA (SK)
    'raba:id',
    -- DCGIS (Washington DC, US)
    'dcgis:gis_id',
    -- Building Identification Number (New York, US)
    'nycdoitt:bin',
    -- Chicago Building Inport (US)
    'chicago:building_id',
    -- Louisville, Kentucky/Building Outlines Import (US)
    'lojic:bgnum',
    -- MassGIS (Massachusetts, US)
    'massgis:way_id',
    -- Los Angeles County building ID (US)
    'lacounty:*',
    -- Address import from Bundesamt für Eich- und Vermessungswesen (AT)
    'at_bev:addr_date',

    -- misc
    'import',
    'import_uuid',
    'OBJTYPE',
    'SK53_bulk:load',
    'mml:class'
}

-- The osm2pgsql.make_clean_tags_func() function takes the list of keys
-- and key prefixes defined above and returns a function that can be used
-- to clean those tags out of a Lua table. The clean_tags function will
-- return true if it removed all tags from the table.
local clean_tags = osm2pgsql.make_clean_tags_func(delete_keys)

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
        or tags['building:part']
end

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    tables.points:add_row({
        tags = object.tags
    })
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    if object.is_closed and has_area_tags(object.tags) then
        tables.polygons:add_row({
            tags = object.tags,
            geom = { create = 'area' }
        })
    else
        tables.lines:add_row({
            tags = object.tags
        })
    end
end

function osm2pgsql.process_relation(object)
    local type = object:grab_tag('type')

    if clean_tags(object.tags) then
        return
    end

    if type == 'route' then
        tables.routes:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })
        return
    end

    if type == 'boundary' or (type == 'multipolygon' and object.tags.boundary) then
        tables.boundaries:add_row({
            tags = object.tags,
            geom = { create = 'line' }
        })
        return
    end

    if type == 'multipolygon' then
        tables.polygons:add_row({
            tags = object.tags,
            geom = { create = 'area' }
        })
    end
end

