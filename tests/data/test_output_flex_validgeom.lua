
local polygons = osm2pgsql.define_table{
    name = 'osm2pgsql_test_polygon',
    ids = { type = 'area', id_column = 'osm_id' },
    columns = {
        { column = 'geom', type = 'geometry' },
    }
}

local function is_empty(some_table)
    return next(some_table) == nil
end

function osm2pgsql.process_way(object)
    if is_empty(object.tags) then
        return
    end

    polygons:insert({
        geom = object:as_polygon()
    })
end

function osm2pgsql.process_relation(object)
    local mgeom = object:as_multipolygon()
    for sgeom in mgeom:geometries() do
        polygons:insert({
            geom = sgeom
        })
    end
end

