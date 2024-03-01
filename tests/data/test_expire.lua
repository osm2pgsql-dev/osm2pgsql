
-- No maxzoom sets it to 0
local eo = osm2pgsql.define_expire_output({
    table = 'osm2pgsql_test_expire',
})

local the_table = osm2pgsql.define_way_table('osm2pgsql_test_t1', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring', expire = {{ output = eo }} },
})

function osm2pgsql.process_way(object)
    if object.tags.t1 then
        the_table:insert{
            tags = object.tags,
            geom = object:as_linestring()
        }
    end
end

