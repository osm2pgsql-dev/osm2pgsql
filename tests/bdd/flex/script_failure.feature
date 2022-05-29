Feature: Handling of errors in the Lua script

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

    Scenario: Missing geom transform in table with geometry should lead to an error
        Given the lua style
            """
            local test_table = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'tags', type = 'hstore' },
                    { column = 'geom', type = 'geometry' },
                    { column = 'area', type = 'area' },
                }
            }

            function osm2pgsql.process_way(object)
                test_table:add_row({
                    tags = object.tags
                })
            end
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Missing geometry transformation for column 'geom'
            """
