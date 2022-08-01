Feature: Null geometry handling

    Scenario: Invalid geometries show up as NULL
        Given the lua style
            """
            local tables = {}

            tables.null = osm2pgsql.define_table{
                name = 'osm2pgsql_test_null',
                ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'geometry', projection = 4326 }
                }
            }

            tables.not_null = osm2pgsql.define_table{
                name = 'osm2pgsql_test_not_null',
                ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'geometry', projection = 4326, not_null = true }
                }
            }

            function osm2pgsql.process_node(object)
                local g = object.as_point()
                tables.null:insert({
                    name = object.tags.name,
                    geom = g
                })
                print("GEOM=", g)
                if g then
                    tables.not_null:insert({
                        name = object.tags.name,
                        geom = g
                    })
                end
            end

            function osm2pgsql.process_way(object)
                local g = object.as_linestring()
                tables.null:insert({
                    name = object.tags.name,
                    geom = g
                })
                print("GEOM=", g)
                if not g:is_null() then
                    tables.not_null:insert({
                        name = object.tags.name,
                        geom = g
                    })
                end
            end
            """

        And the grid
            | 10 |   | 11 |

        And the OSM data
            """
            n12 x3.4 y5.6 Tname=valid
            n13 x42 y42
            n14 x42 y42
            w20 Tname=valid Nn10,n11
            w21 Tname=invalid Nn10
            w22 Tname=invalid Nn13,n13
            w23 Tname=invalid Nn13,n14
            """

        When running osm2pgsql flex

        Then table osm2pgsql_test_null contains exactly
            | osm_type | osm_id | ST_AsText(geom) |
            | N        | 12     | 3.4 5.6         |
            | W        | 20     | 10, 11          |
            | W        | 21     | NULL            |
            | W        | 22     | NULL            |
            | W        | 23     | NULL            |

        And table osm2pgsql_test_not_null contains exactly
            | osm_type | osm_id | ST_AsText(geom) |
            | N        | 12     | 3.4 5.6         |
            | W        | 20     | 10, 11          |

