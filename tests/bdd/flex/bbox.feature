Feature: Test get_bbox() function

    Scenario Outline: for nodes
        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x20.0 y10.1
            """
        And the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'min_x', type = 'real' },
                { column = 'min_y', type = 'real' },
                { column = 'max_x', type = 'real' },
                { column = 'max_y', type = 'real' },
                { column = 'geom',  type = 'point', projection = <projection> },
            })

            function osm2pgsql.process_node(object)
                local row = {}
                row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()
                points:add_row(row)
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_points contains exactly
            | node_id | min_x | max_x | min_y | max_y | ST_AsText(geom) |
            | 10      | 20.0  | 20.0  | 10.1  | 10.1  | <geometry>      |

        Examples:
            | projection | geometry            |
            | 4326       | 20 10.1             |
            | 3857       | 2226389.8 1130195.4 |

    Scenario Outline: for ways
        Given the 0.1 grid with origin 20.0 10.1
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            w20 v1 dV Thighway=primary Nn10,n11,n12
            """
        And the lua style
            """
            local highways = osm2pgsql.define_way_table('osm2pgsql_test_highways', {
                { column = 'min_x', type = 'real' },
                { column = 'min_y', type = 'real' },
                { column = 'max_x', type = 'real' },
                { column = 'max_y', type = 'real' },
                { column = 'geom',  type = 'linestring', projection = <projection> },
            })

            function osm2pgsql.process_way(object)
                local row = { geom = { create = 'line' } }
                row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()
                highways:add_row(row)
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_highways contains exactly
            | way_id | min_x | max_x | min_y | max_y | ST_AsText(geom) |
            | 20     | 20.0  | 20.1  | 10.0  | 10.1  | <geometry>      |

        Examples:
            | projection | geometry                                                    |
            | 4326       | 20 10.1,20.1 10.1,20.1 10                                   |
            | 3857       | 2226389.8 1130195.4,2237521.8 1130195.4,2237521.8 1118890.0 |

    Scenario Outline: for relations
        Given the 0.1 grid with origin 20.0 10.1
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11
            w21 v1 dV Nn11,n12
            r30 v1 dV Ttype=route,route=bus Mw20@@
            r31 v1 dV Ttype=route,route=bus Mw20@,w21@
            """
        And the lua style
            """
            local rels = osm2pgsql.define_relation_table('osm2pgsql_test_routes', {
                { column = 'min_x', type = 'real' },
                { column = 'min_y', type = 'real' },
                { column = 'max_x', type = 'real' },
                { column = 'max_y', type = 'real' },
                { column = 'geom',  type = 'linestring', projection = <projection> },
            })

            function osm2pgsql.process_relation(object)
                local row = { geom = { create = 'line' } }
                row.min_x, row.min_y, row.max_x, row.max_y = object:get_bbox()
                rels:add_row(row)
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_routes contains exactly
            | relation_id | min_x | max_x | min_y | max_y | ST_AsText(geom) |
            | 30          | 20.0  | 20.1  | 10.1  | 10.1  | <geom30>        |
            | 31          | 20.0  | 20.1  | 10.0  | 10.1  | <geom31>        |

        Examples:
            | projection | geom30                                  | geom31                                                      |
            | 4326       | 10, 11                                  | 10, 11, 12                                                  |
            | 3857       | 2226389.8 1130195.4,2237521.8 1130195.4 | 2226389.8 1130195.4,2237521.8 1130195.4,2237521.8 1118890.0 |

