Feature: Tests that empty insert command does something

    Scenario:
        Given the OSM data
            """
            n1 Tnatural=water x1 y2
            """
        And the lua style
            """
            local points = osm2pgsql.define_table{
                name = 'osm2pgsql_test_points',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'geom', type = 'point' },
                }
            }

            function osm2pgsql.process_node(object)
                points:insert()
            end
            """

        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Need two parameters
            """

