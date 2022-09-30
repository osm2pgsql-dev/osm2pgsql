Feature: Creating (multi)point features from nodes and relations

    Scenario:
        Given the grid
            | 1 | 2 |   |
            | 4 |   | 3 |
            |   | 5 | 6 |
        And the OSM data
            """
            n1 Thighway=bus_stop
            n5 Thighway=bus_stop
            w20 Thighway=residential Nn1,n2,n3,n4
            w21 Thighway=primary Nn4,n5,n6
            r30 Troute=bus Mn1@
            r31 Troute=bus Mw21@,n5@,w20@,n1@
            """
        And the lua style
            """
            local points = osm2pgsql.define_table({
                name = 'osm2pgsql_test_points',
                ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
                columns = {
                    { column = 'geom', type = 'geometry', projection = 4326 },
                }
            })

            function osm2pgsql.process_node(object)
                if object.tags.highway == 'bus_stop' then
                    points:insert({
                        geom = object:as_multipoint()
                    })
                end
            end

            function osm2pgsql.process_relation(object)
                if object.tags.route == 'bus' then
                    points:insert({
                        geom = object:as_multipoint()
                    })
                end
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_points contains exactly
            | osm_type | osm_id | ST_GeometryType(geom) | ST_NumGeometries(geom) | ST_AsText(ST_GeometryN(geom, 1)) | ST_AsText(ST_GeometryN(geom, 2)) |
            | N        | 1      | ST_Point              | 1                      | 1                                | NULL                             |
            | N        | 5      | ST_Point              | 1                      | 5                                | NULL                             |
            | R        | 30     | ST_Point              | 1                      | 1                                | NULL                             |
            | R        | 31     | ST_MultiPoint         | 2                      | 5                                | 1                                |

