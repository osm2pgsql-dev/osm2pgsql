Feature: Diff expire

    Scenario: non-diff expire way node changes with diff_expire disabled
        Given the OSM data
            """
            n1 v1 x0 y0
            n2 v1 x2 y0
            n3 v1 x2 y1
            n4 v1 x4 y1
            w1 v1 Thighway=primary Nn1,n2,n3,n4
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_way_table('osm2pgsql_test', {
                { column = 'geom', type = 'linestring', expire = {
                        { output = eo, diff_expire = false }
                    }
                },
            })

            function osm2pgsql.process_way(object)
                the_table:insert{
                    geom = object:as_linestring()
                }
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,222638.98158654713 0,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            n2 v2 x0 y1
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,0 111325.14285463623,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 127 | 127 |
            | 8    | 128 | 127 |
            | 8    | 129 | 127 |
            | 8    | 130 | 127 |
            | 8    | 127 | 128 |
            | 8    | 128 | 128 |
            | 8    | 129 | 128 |

    Scenario: non-diff expire if way changes
        Given the OSM data
            """
            n1 v1 x0 y0
            n2 v1 x2 y0
            n3 v1 x2 y1
            n4 v1 x4 y1
            w1 v1 Thighway=primary Nn1,n2,n3,n4
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_way_table('osm2pgsql_test', {
                { column = 'geom', type = 'linestring', expire = {
                        { output = eo, diff_expire = true }
                    }
                },
            })

            function osm2pgsql.process_way(object)
                the_table:insert{
                    geom = object:as_linestring()
                }
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,222638.98158654713 0,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            w1 v2 Thighway=secondary Nn1,n2,n3,n4
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,222638.98158654713 0,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 127 | 127 |
            | 8    | 128 | 127 |
            | 8    | 129 | 127 |
            | 8    | 130 | 127 |
            | 8    | 127 | 128 |
            | 8    | 128 | 128 |
            | 8    | 129 | 128 |

    Scenario: diff expire way node changes with diff_expire enabled
        Given the OSM data
            """
            n1 v1 x0 y0
            n2 v1 x2 y0
            n3 v1 x2 y1
            n4 v1 x4 y1
            w1 v1 Thighway=primary Nn1,n2,n3,n4
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_way_table('osm2pgsql_test', {
                { column = 'geom', type = 'linestring', expire = {
                        { output = eo, diff_expire = true }
                    }
                },
            })

            function osm2pgsql.process_way(object)
                the_table:insert{
                    geom = object:as_linestring()
                }
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,222638.98158654713 0,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            n2 v2 x0 y1
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo |
            | 1      | 0 0,0 111325.14285463623,222638.98158654713 111325.14285463623,445277.96317309426 111325.14285463623 |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 127 | 127 |
            | 8    | 128 | 127 |
            | 8    | 129 | 127 |
            | 8    | 127 | 128 |
            | 8    | 128 | 128 |
            | 8    | 129 | 128 |

    Scenario: non-diff expire of relation when way changes with diff_expire disabled
        Given the OSM data
            """
            n11 v1 x0 y0
            n12 v1 x1 y0
            n13 v1 x1 y1
            n14 v1 x0 y1
            n15 v1 x2 y2
            n16 v1 x3 y2
            n17 v1 x3 y3
            n18 v1 x2 y3
            w20 v1 Nn11,n12,n13,n14,n11
            w21 v1 Nn15,n16,n17,n18,n15
            r30 v1 Ttype=multipolygon Mw20@,w21@
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_relation_table('osm2pgsql_test', {
                { column = 'geom', type = 'polygon', expire = {
                        { output = eo, diff_expire = false }
                    }
                },
            })

            function osm2pgsql.process_relation(object)
                for geom in object:as_multipolygon():geometries() do
                    the_table:insert{
                        geom = geom
                    }
                end
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            w21 v2 Nn15,n16,n17,n15
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 127 | 127 |
            | 8    | 128 | 127 |
            | 8    | 127 | 128 |
            | 8    | 128 | 128 |
            | 8    | 129 | 125 |
            | 8    | 130 | 125 |
            | 8    | 129 | 126 |
            | 8    | 130 | 126 |

    Scenario: non-diff expire of relation when relation changes
        Given the OSM data
            """
            n11 v1 x0 y0
            n12 v1 x1 y0
            n13 v1 x1 y1
            n14 v1 x0 y1
            n15 v1 x2 y2
            n16 v1 x3 y2
            n17 v1 x3 y3
            n18 v1 x2 y3
            w20 v1 Nn11,n12,n13,n14,n11
            w21 v1 Nn15,n16,n17,n18,n15
            r30 v1 Ttype=multipolygon Mw20@,w21@
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_relation_table('osm2pgsql_test', {
                { column = 'geom', type = 'polygon', expire = {
                        { output = eo, diff_expire = true }
                    }
                },
            })

            function osm2pgsql.process_relation(object)
                for geom in object:as_multipolygon():geometries() do
                    the_table:insert{
                        geom = geom
                    }
                end
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            r30 v2 Ttype=multipolygon,landuse=forest Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 127 | 127 |
            | 8    | 128 | 127 |
            | 8    | 127 | 128 |
            | 8    | 128 | 128 |
            | 8    | 129 | 125 |
            | 8    | 130 | 125 |
            | 8    | 129 | 126 |
            | 8    | 130 | 126 |

    Scenario: non-diff expire of relation when way changes with diff_expire enabled
        Given the OSM data
            """
            n11 v1 x0 y0
            n12 v1 x1 y0
            n13 v1 x1 y1
            n14 v1 x0 y1
            n15 v1 x2 y2
            n16 v1 x3 y2
            n17 v1 x3 y3
            n18 v1 x2 y3
            w20 v1 Nn11,n12,n13,n14,n11
            w21 v1 Nn15,n16,n17,n18,n15
            r30 Ttype=multipolygon Mw20@,w21@
            """
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 8,
            })

            local the_table = osm2pgsql.define_relation_table('osm2pgsql_test', {
                { column = 'geom', type = 'polygon', expire = {
                        { output = eo, diff_expire = true }
                    }
                },
            })

            function osm2pgsql.process_relation(object)
                for geom in object:as_multipolygon():geometries() do
                    the_table:insert{
                        geom = geom
                    }
                end
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table osm2pgsql_test has 2 rows
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire has 0 rows

        Given the OSM data
            """
            w21 v2 Nn15,n16,n17,n15
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test contains exactly
            | relation_id | geom!geo |
            | 30          | (0 0,111319.49079327357 0,111319.49079327357 111325.14285463623,0 111325.14285463623,0 0) |
            | 30          | (222638.98158654713 222684.20848178727,333958.4723798207 222684.20848178727,333958.4723798207 334111.17136656796,222638.98158654713 222684.20848178727) |
        Then table osm2pgsql_test_expire contains exactly
            | zoom |   x |   y |
            | 8    | 129 | 125 |
            | 8    | 130 | 125 |
            | 8    | 129 | 126 |
            | 8    | 130 | 126 |

