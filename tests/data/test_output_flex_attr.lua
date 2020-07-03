
local attr_table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_attr',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'tags', type = 'hstore' },
        { column = 'version', type = 'int4' },
        { column = 'changeset', type = 'int4' },
        { column = 'timestamp', type = 'int4' },
        { column = 'uid', type = 'int4' },
        { column = 'user', type = 'text' },
        { column = 'geom', type = 'linestring' },
    }
}

function osm2pgsql.process_way(object)
    object.geom = { create = 'line' }
    attr_table:add_row(object)
end

