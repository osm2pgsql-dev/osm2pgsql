-- This config example file is released into the Public Domain.

-- This configuration for the flex output tries to be compatible with the
-- original pgsql C transform output. There might be some corner cases but
-- it should do exactly the same in almost all cases.

-- The output projection used (3857, web mercator is the default). Set this
-- to 4326 if you were using the -l|--latlong option or to the EPSG
-- code you were using on the -E|-proj option.
local srid = 3857

-- Set this to true if you were using option -K|--keep-coastlines.
local keep_coastlines = false

-- Set this to the table name prefix (what used to be option -p|--prefix).
local prefix = 'planet_osm'

-- Set this to true if multipolygons should be written as multipolygons into
-- db (what used to be option -G|--multi-geometry).
local multi_geometry = false

-- Set this to true if you want an hstore column (what used to be option
-- -k|--hstore). Can not be true if "hstore_all" is true.
local hstore = false

-- Set this to true if you want all tags in an hstore column (what used to
-- be option -j|--hstore-all). Can not be true if "hstore" is true.
local hstore_all = false

-- Only keep objects that have a value in one of the non-hstore columns
-- (normal action with --hstore is to keep all objects). Equivalent to
-- what used to be set through option --hstore-match-only.
local hstore_match_only = false

-- Set this to add an additional hstore (key/value) column containing all tags
-- that start with the specified string, eg "name:". Will produce an extra
-- hstore column that contains all "name:xx" tags. Equivalent to what used to
-- be set through option -z|--hstore-column. Unlike the -z option which can
-- be specified multiple time, this does only support a single additional
-- hstore column.
local hstore_column = nil

-- There is some very old specialized handling of route relations in osm2pgsql,
-- which you probably don't need. This is disabled here, but you can enable
-- it by setting this to true. If you don't understand this, leave it alone.
local enable_legacy_route_processing = false

-- ---------------------------------------------------------------------------

if hstore and hstore_all then
    error("hstore and hstore_all can't be both true")
end

-- Used for splitting up long linestrings
if srid == 4326 then
    max_length = 1
else
    max_length = 100000
end

-- Ways with any of the following keys will be treated as polygon
local polygon_keys = {
    'aeroway',
    'amenity',
    'building',
    'harbour',
    'historic',
    'landuse',
    'leisure',
    'man_made',
    'military',
    'natural',
    'office',
    'place',
    'power',
    'public_transport',
    'shop',
    'sport',
    'tourism',
    'water',
    'waterway',
    'wetland',
    'abandoned:aeroway',
    'abandoned:amenity',
    'abandoned:building',
    'abandoned:landuse',
    'abandoned:power',
    'area:highway'
}

-- Objects without any of the following keys will be deleted
local generic_keys = {
    'access',
    'addr:housename',
    'addr:housenumber',
    'addr:interpolation',
    'admin_level',
    'aerialway',
    'aeroway',
    'amenity',
    'area',
    'barrier',
    'bicycle',
    'boundary',
    'brand',
    'bridge',
    'building',
    'capital',
    'construction',
    'covered',
    'culvert',
    'cutting',
    'denomination',
    'disused',
    'ele',
    'embankment',
    'foot',
    'generation:source',
    'harbour',
    'highway',
    'historic',
    'hours',
    'intermittent',
    'junction',
    'landuse',
    'layer',
    'leisure',
    'lock',
    'man_made',
    'military',
    'motorcar',
    'name',
    'natural',
    'office',
    'oneway',
    'operator',
    'place',
    'population',
    'power',
    'power_source',
    'public_transport',
    'railway',
    'ref',
    'religion',
    'route',
    'service',
    'shop',
    'sport',
    'surface',
    'toll',
    'tourism',
    'tower:type',
    'tracktype',
    'tunnel',
    'water',
    'waterway',
    'wetland',
    'width',
    'wood',
    'abandoned:aeroway',
    'abandoned:amenity',
    'abandoned:building',
    'abandoned:landuse',
    'abandoned:power',
    'area:highway'
}

-- The following keys will be deleted
local delete_keys = {
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
    'way',
    'way_area',
    'z_order',
}

local point_columns = {
    'access',
    'addr:housename',
    'addr:housenumber',
    'addr:interpolation',
    'admin_level',
    'aerialway',
    'aeroway',
    'amenity',
    'area',
    'barrier',
    'bicycle',
    'brand',
    'bridge',
    'boundary',
    'building',
    'capital',
    'construction',
    'covered',
    'culvert',
    'cutting',
    'denomination',
    'disused',
    'ele',
    'embankment',
    'foot',
    'generator:source',
    'harbour',
    'highway',
    'historic',
    'horse',
    'intermittent',
    'junction',
    'landuse',
    'layer',
    'leisure',
    'lock',
    'man_made',
    'military',
    'motorcar',
    'name',
    'natural',
    'office',
    'oneway',
    'operator',
    'place',
    'population',
    'power',
    'power_source',
    'public_transport',
    'railway',
    'ref',
    'religion',
    'route',
    'service',
    'shop',
    'sport',
    'surface',
    'toll',
    'tourism',
    'tower:type',
    'tunnel',
    'water',
    'waterway',
    'wetland',
    'width',
    'wood',
}

local non_point_columns = {
    'access',
    'addr:housename',
    'addr:housenumber',
    'addr:interpolation',
    'admin_level',
    'aerialway',
    'aeroway',
    'amenity',
    'area',
    'barrier',
    'bicycle',
    'brand',
    'bridge',
    'boundary',
    'building',
    'construction',
    'covered',
    'culvert',
    'cutting',
    'denomination',
    'disused',
    'embankment',
    'foot',
    'generator:source',
    'harbour',
    'highway',
    'historic',
    'horse',
    'intermittent',
    'junction',
    'landuse',
    'layer',
    'leisure',
    'lock',
    'man_made',
    'military',
    'motorcar',
    'name',
    'natural',
    'office',
    'oneway',
    'operator',
    'place',
    'population',
    'power',
    'power_source',
    'public_transport',
    'railway',
    'ref',
    'religion',
    'route',
    'service',
    'shop',
    'sport',
    'surface',
    'toll',
    'tourism',
    'tower:type',
    'tracktype',
    'tunnel',
    'water',
    'waterway',
    'wetland',
    'width',
    'wood',
}

function gen_columns(text_columns, with_hstore, area, geometry_type)
    columns = {}

    local add_column = function (name, type)
        columns[#columns + 1] = { column = name, type = type }
    end

    for _, c in ipairs(text_columns) do
        add_column(c, 'text')
    end

    add_column('z_order', 'int')

    if area ~= nil then
        if area then
            add_column('way_area', 'area')
        else
            add_column('way_area', 'real')
        end
    end

    if hstore_column then
        add_column(hstore_column, 'hstore')
    end

    if with_hstore then
        add_column('tags', 'hstore')
    end

    add_column('way', geometry_type)
    columns[#columns].projection = srid
    columns[#columns].not_null = true

    return columns
end

local tables = {}

tables.point = osm2pgsql.define_table{
    name = prefix .. '_point',
    ids = { type = 'node', id_column = 'osm_id' },
    columns = gen_columns(point_columns, hstore or hstore_all, nil, 'point')
}

tables.line = osm2pgsql.define_table{
    name = prefix .. '_line',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = gen_columns(non_point_columns, hstore or hstore_all, false, 'linestring')
}

tables.polygon = osm2pgsql.define_table{
    name = prefix .. '_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = gen_columns(non_point_columns, hstore or hstore_all, true, 'geometry')
}

tables.roads = osm2pgsql.define_table{
    name = prefix .. '_roads',
    ids = { type = 'way', id_column = 'osm_id' },
    columns = gen_columns(non_point_columns, hstore or hstore_all, false, 'linestring')
}

local z_order_lookup = {
    proposed = {1, false},
    construction = {2, false},
    steps = {10, false},
    cycleway = {10, false},
    bridleway = {10, false},
    footway = {10, false},
    path = {10, false},
    track = {11, false},
    service = {15, false},

    tertiary_link = {24, false},
    secondary_link = {25, true},
    primary_link = {27, true},
    trunk_link = {28, true},
    motorway_link = {29, true},

    raceway = {30, false},
    pedestrian = {31, false},
    living_street = {32, false},
    road = {33, false},
    unclassified = {33, false},
    residential = {33, false},
    tertiary = {34, false},
    secondary = {36, true},
    primary = {37, true},
    trunk = {38, true},
    motorway = {39, true}
}

function as_bool(value)
    return value == 'yes' or value == 'true' or value == '1'
end

function get_z_order(tags)
    local z_order = 100 * math.floor(tonumber(tags.layer or '0') or 0)
    local roads = false

    local highway = tags['highway']
    if highway then
        local r = z_order_lookup[highway] or {0, false}
        z_order = z_order + r[1]
        roads = r[2]
    end

    if tags.railway then
        z_order = z_order + 35
        roads = true
    end

    if tags.boundary and tags.boundary == 'administrative' then
        roads = true
    end

    if as_bool(tags.bridge) then
        z_order = z_order + 100
    end

    if as_bool(tags.tunnel) then
        z_order = z_order - 100
    end

    return z_order, roads
end

function make_check_in_list_func(list)
    local h = {}
    for _, k in ipairs(list) do
        h[k] = true
    end
    return function(tags)
        for k, _ in pairs(tags) do
            if h[k] then
                return true
            end
        end
        return false
    end
end

local is_polygon = make_check_in_list_func(polygon_keys)
local clean_tags = osm2pgsql.make_clean_tags_func(delete_keys)

function make_column_hash(columns)
    local h = {}

    for _, k in ipairs(columns) do
        h[k] = true
    end

    return h
end

function make_get_output(columns, hstore_all)
    local h = make_column_hash(columns)
    if hstore_all then
        return function(tags)
            local output = {}
            local hstore_entries = {}

            for k, _ in pairs(tags) do
                if h[k] then
                    output[k] = tags[k]
                end
                hstore_entries[k] = tags[k]
            end

            return output, hstore_entries
        end
    else
        return function(tags)
            local output = {}
            local hstore_entries = {}

            for k, _ in pairs(tags) do
                if h[k] then
                    output[k] = tags[k]
                else
                    hstore_entries[k] = tags[k]
                end
            end

            return output, hstore_entries
        end
    end
end

local has_generic_tag = make_check_in_list_func(generic_keys)

local get_point_output = make_get_output(point_columns, hstore_all)
local get_non_point_output = make_get_output(non_point_columns, hstore_all)

function get_hstore_column(tags)
    local len = #hstore_column
    local h = {}
    for k, v in pairs(tags) do
        if k:sub(1, len) == hstore_column then
            h[k:sub(len + 1)] = v
        end
    end

    if next(h) then
        return h
    end
    return nil
end

function osm2pgsql.process_node(object)
    if clean_tags(object.tags) then
        return
    end

    local output
    local output_hstore = {}
    if hstore or hstore_all then
        output, output_hstore = get_point_output(object.tags)
        if not next(output) and not next(output_hstore) then
            return
        end
        if hstore_match_only and not has_generic_tag(object.tags) then
            return
        end
    else
        output = object.tags
        if not has_generic_tag(object.tags) then
            return
        end
    end

    output.tags = output_hstore

    if hstore_column then
        output[hstore_column] = get_hstore_column(object.tags)
    end

    output.way = object:as_point()
    tables.point:insert(output)
end

function add_line(output, geom, roads)
    for sgeom in geom:segmentize(max_length):geometries() do
        output.way = sgeom
        tables.line:insert(output)
        if roads then
            tables.roads:insert(output)
        end
    end
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    local add_area = false
    if object.tags.natural == 'coastline' then
        add_area = true
        if not keep_coastlines then
            object.tags.natural = nil
        end
    end

    local output
    local output_hstore = {}
    if hstore or hstore_all then
        output, output_hstore = get_non_point_output(object.tags)
        if not next(output) and not next(output_hstore) then
            return
        end
        if hstore_match_only and not has_generic_tag(object.tags) then
            return
        end
        if add_area and hstore_all then
            output_hstore.area = 'yes'
        end
    else
        output = object.tags
        if not has_generic_tag(object.tags) then
            return
        end
    end

    local polygon
    local area_tag = object.tags.area
    if area_tag == 'yes' or area_tag == '1' or area_tag == 'true' then
        polygon = true
    elseif area_tag == 'no' or area_tag == '0' or area_tag == 'false' then
        polygon = false
    else
        polygon = is_polygon(object.tags)
    end

    if add_area then
        output.area = 'yes'
        polygon = true
    end

    local z_order, roads = get_z_order(object.tags)
    output.z_order = z_order

    output.tags = output_hstore

    if hstore_column then
        output[hstore_column] = get_hstore_column(object.tags)
    end

    if polygon and object.is_closed then
        output.way = object:as_polygon()
        tables.polygon:insert(output)
    else
        add_line(output, object:as_linestring(), roads)
    end
end

function osm2pgsql.process_relation(object)
    if clean_tags(object.tags) then
        return
    end

    local rtype = object:grab_tag('type')
    if (rtype ~= 'route') and (rtype ~= 'multipolygon') and (rtype ~= 'boundary') then
        return
    end

    local output
    local output_hstore = {}
    if hstore or hstore_all then
        output, output_hstore = get_non_point_output(object.tags)
        if not next(output) and not next(output_hstore) then
            return
        end
        if hstore_match_only and not has_generic_tag(object.tags) then
            return
        end
    else
        output = object.tags
        if not has_generic_tag(object.tags) then
            return
        end
    end

    if not next(output) and not next(output_hstore) then
        return
    end

    if enable_legacy_route_processing and (hstore or hstore_all) and rtype == 'route' then
        if not object.tags.route_name then
            output_hstore.route_name = object.tags.name
        end

        local state = object.tags.state
        if state ~= 'alternate' and state ~= 'connection' then
            state = 'yes'
        end

        local network = object.tags.network
        if network == 'lcn' then
            output_hstore.lcn = output_hstore.lcn or state
            output_hstore.lcn_ref = output_hstore.lcn_ref or object.tags.ref
        elseif network == 'rcn' then
            output_hstore.rcn = output_hstore.rcn or state
            output_hstore.rcn_ref = output_hstore.rcn_ref or object.tags.ref
        elseif network == 'ncn' then
            output_hstore.ncn = output_hstore.ncn or state
            output_hstore.ncn_ref = output_hstore.ncn_ref or object.tags.ref
        elseif network == 'lwn' then
            output_hstore.lwn = output_hstore.lwn or state
            output_hstore.lwn_ref = output_hstore.lwn_ref or object.tags.ref
        elseif network == 'rwn' then
            output_hstore.rwn = output_hstore.rwn or state
            output_hstore.rwn_ref = output_hstore.rwn_ref or object.tags.ref
        elseif network == 'nwn' then
            output_hstore.nwn = output_hstore.nwn or state
            output_hstore.nwn_ref = output_hstore.nwn_ref or object.tags.ref
        end

        local pc = object.tags.preferred_color
        if pc == '0' or pc == '1' or pc == '2' or pc == '3' or pc == '4' then
            output_hstore.route_pref_color = pc
        else
            output_hstore.route_pref_color = '0'
        end
    end

    local make_boundary = false
    local make_polygon = false
    if rtype == 'boundary' then
        make_boundary = true
    elseif rtype == 'multipolygon' and object.tags.boundary then
        make_boundary = true
    elseif rtype == 'multipolygon' then
        make_polygon = true
    end

    local z_order, roads = get_z_order(object.tags)
    output.z_order = z_order

    output.tags = output_hstore

    if hstore_column then
        output[hstore_column] = get_hstore_column(object.tags)
    end

    if not make_polygon then
        add_line(output, object:as_multilinestring(), roads)
    end

    if make_boundary or make_polygon then
        local geom = object:as_multipolygon()
        if multi_geometry then
            output.way = geom
            tables.polygon:insert(output)
        else
            for sgeom in geom:geometries() do
                output.way = sgeom
                tables.polygon:insert(output)
            end
        end
    end
end

