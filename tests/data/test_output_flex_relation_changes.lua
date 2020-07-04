
local rel_table = osm2pgsql.define_area_table('osm2pgsql_test_relations', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'geometry' }
})

function osm2pgsql.process_relation(object)
    if object.tags.type == 'multipolygon' then
        rel_table:add_row{
            tags = object.tags,
            geom = { create = 'area' }
        }
    end
end

