
local eo = osm2pgsql.define_expire_output({
    table = 'osm2pgsql_test_expire',
    maxzoom = 2,
    max_tiles_geometry = 2,
    max_tiles_overall = 6,
})

local the_table = osm2pgsql.define_way_table('osm2pgsql_test_t1', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring', expire = {{ output = eo }} },
})

function osm2pgsql.process_way(object)
    the_table:insert{
        tags = object.tags,
        geom = object:as_linestring()
    }
end

