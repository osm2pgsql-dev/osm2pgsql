Feature: Test get_bbox() function

    Scenario Outline:
        Given the 0.1 grid with origin 20.0 10.1
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            w20 v1 dV Thighway=primary Nn10,n11,n12
            """
        And the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'tags',  type = 'hstore' },
                { column = 'min_x', type = 'real' },
                { column = 'min_y', type = 'real' },
                { column = 'max_x', type = 'real' },
                { column = 'max_y', type = 'real' },
                { column = 'geom',  type = 'point', projection = <projection> },
            })

            local highways = osm2pgsql.define_way_table('osm2pgsql_test_highways', {
                { column = 'tags',  type = 'hstore' },
                { column = 'min_x', type = 'real' },
                { column = 'min_y', type = 'real' },
                { column = 'max_x', type = 'real' },
                { column = 'max_y', type = 'real' },
                { column = 'geom',  type = 'linestring', projection = <projection> },
            })

            function osm2pgsql.process_node(object)
                local row = {
                    tags = object.tags,
                }

                row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()

                points:add_row(row)
            end

            function osm2pgsql.process_way(object)
                local row = {
                    tags = object.tags,
                    geom = { create = 'line' }
                }

                row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()

                highways:add_row(row)
            end
            """
        When running osm2pgsql flex with parameters
            | -x |

        Then table osm2pgsql_test_points contains
            | node_id | min_x | max_x | min_y | max_y |
            | 10      | 20.0  | 20.0  | 10.1  | 10.1  |
            | 11      | 20.1  | 20.1  | 10.1  | 10.1  |
            | 12      | 20.1  | 20.1  | 10.0  | 10.0  |

        And table osm2pgsql_test_highways contains
            | way_id | min_x | max_x | min_y | max_y | ST_AsText(geom) |
            | 20     | 20.0  | 20.1  | 10.0  | 10.1  | <geometry> |

        Examples:
            | projection | geometry |
            | 4326       | 20 10.1,20.1 10.1,20.1 10 |
            | 3857       | 2226389.8 1130195.4,2237521.8 1130195.4,2237521.8 1118890.0 |
