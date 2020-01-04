
table = osm2pgsql.define_table{
    name = 'osm2pgsql_test_ways_attr',
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

function osm2pgsql.process_way(data)
    data.geom = { create = 'line' }
    table:add_row(data)
end

