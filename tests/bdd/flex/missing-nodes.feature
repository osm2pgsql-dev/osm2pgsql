Feature: Test handling of missing nodes

    Background:
        Given the lua style
            """
            local tables = {}

            tables.line = osm2pgsql.define_table{
                name = 'osm2pgsql_test_lines',
                ids = { type = 'way', id_column = 'osm_id' },
                columns = {
                    { column = 'geom', type = 'linestring', projection = 4326 },
                }
            }

            function osm2pgsql.process_way(object)
                tables.line:insert({
                    geom = object:as_linestring()
                })
            end
            """

    Scenario: Missing node is reported
        Given the OSM data
            """
            n10 v1 dV x10.0 y10.0
            n11 v1 dV x10.0 y11.0
            w20 v1 dV Thighway=primary Nn10,n11,n12,n13
            """
        When running osm2pgsql flex with parameters
            | --log-level=debug |

        Then table osm2pgsql_test_lines has 1 rows

        And the error output contains
            """
            Missing nodes in way 20: 12,13
            """

