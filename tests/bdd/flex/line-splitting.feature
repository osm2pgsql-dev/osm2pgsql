Feature: Test splitting of lines

    Scenario: Import linestring in latlon projection (unsplit and split)
        Given the lua style
            """
            local tables = {}

            tables.line = osm2pgsql.define_way_table('osm2pgsql_test_line', {
                { column = 'tags', type = 'hstore' },
                { column = 'geom', type = 'linestring', projection = 4326 }
            })

            tables.split = osm2pgsql.define_way_table('osm2pgsql_test_split', {
                { column = 'tags', type = 'hstore' },
                { column = 'geom', type = 'linestring', projection = 4326 }
            })

            function osm2pgsql.process_way(object)
                tables.line:add_row({
                    tags = object.tags,
                    geom = { create = 'line' }
                })
                tables.split:add_row({
                    tags = object.tags,
                    geom = { create = 'line', split_at = 1.0 }
                })
            end
            """

        Given the 0.5 grid
            | 10 |   | 11 |   |   | 12 |
        And the OSM data
            """
            w20 v1 dV Thighway=primary Nn10,n11
            w21 v1 dV Thighway=primary Nn10,n12
            """

        When running osm2pgsql flex

        Then table osm2pgsql_test_line contains exactly
            | way_id | ST_Length(geom) | ST_AsText(geom) |
            | 20     | 1.0             | 10, 11          |
            | 21     | 2.5             | 10, 12          |

        And table osm2pgsql_test_split contains exactly
            | way_id | ST_Length(geom) | ST_AsText(geom)      |
            | 20     | 1.0             | 10, 11               |
            | 21     | 1.0             | 20.0 20.0, 21.0 20.0 |
            | 21     | 1.0             | 21.0 20.0, 22.0 20.0 |
            | 21     | 0.5             | 22.0 20.0, 22.5 20.0 |

