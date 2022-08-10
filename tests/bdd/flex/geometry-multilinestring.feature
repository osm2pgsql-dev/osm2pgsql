Feature: Creating (multi)linestring features from way and relations

    Scenario:
        Given the grid
            | 1 | 2 |   |
            | 4 |   | 3 |
            |   | 5 | 6 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2,n3
            w21 Thighway=motorway Nn4,n5,n6
            r30 Ttype=route,route=road Mw20@
            r31 Ttype=route,route=road Mw20@,w21@
            """
        And the lua style
            """
            local lines = osm2pgsql.define_table({
                name = 'osm2pgsql_test_lines',
                ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
                columns = {
                    { column = 'geom', type = 'geometry', projection = 4326 },
                }
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway == 'motorway' then
                    lines:insert({
                        geom = object:as_multilinestring()
                    })
                end
            end

            function osm2pgsql.process_relation(object)
                if object.tags.type == 'route' then
                    lines:insert({
                        geom = object:as_multilinestring()
                    })
                end
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_lines contains exactly
            | osm_type | osm_id | ST_GeometryType(geom) | ST_AsText(ST_GeometryN(geom, 1)) | ST_AsText(ST_GeometryN(geom, 2)) |
            | W        | 20     | ST_LineString         | 1, 2, 3                          | NULL                             |
            | W        | 21     | ST_LineString         | 4, 5, 6                          | NULL                             |
            | R        | 30     | ST_LineString         | 1, 2, 3                          | NULL                             |
            | R        | 31     | ST_MultiLineString    | 1, 2, 3                          | 4, 5, 6                          |

