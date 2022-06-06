Feature: Handling of multiple geometries

    Background:
        Given the 1.0 grid
            | 13 | 12 |   | 17 | 16 |
            | 10 | 11 |   | 14 | 15 |
        And the OSM data
            """
            w20 Tnatural=water,name=poly Nn10,n11,n12,n13,n10
            w21 Nn10,n11,n12,n13,n10
            w22 Nn14,n15,n16,n17,n14
            r30 Ttype=multipolygon,natural=water,name=poly Mw21@outer
            r31 Ttype=multipolygon,natural=water,name=multi Mw21@outer,w22@outer
            """

    Scenario: Use 'geometry' column for area (not splitting multipolygons)
        Given the lua style
            """
            local polygons = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'geometry' }
                }
            }

            function osm2pgsql.process_way(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end

            function osm2pgsql.process_relation(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_polygon contains exactly
            | osm_id | ST_GeometryType(geom) |
            | 20     | ST_Polygon            |
            | -30    | ST_Polygon            |
            | -31    | ST_MultiPolygon       |


    Scenario Outline: Use 'geometry'/'polygon' column for area (splitting multipolygons)
        Given the lua style
            """
            local polygons = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = '<geometry_type>' }
                }
            }

            function osm2pgsql.process_way(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end

            function osm2pgsql.process_relation(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area', split_at = 'multi' }
                })
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_polygon contains exactly
            | osm_id | ST_GeometryType(geom) |
            | 20     | ST_Polygon            |
            | -30    | ST_Polygon            |
            | -31    | ST_Polygon            |
            | -31    | ST_Polygon            |

        Examples:
            | geometry_type |
            | geometry      |
            | polygon       |


    Scenario: Use 'multipolygon' column for area (not splitting multipolygons)
        Given the lua style
            """
            local polygons = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'multipolygon' }
                }
            }

            function osm2pgsql.process_way(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end

            function osm2pgsql.process_relation(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_polygon contains exactly
            | osm_id | ST_GeometryType(geom) |
            | 20     | ST_MultiPolygon       |
            | -30    | ST_MultiPolygon       |
            | -31    | ST_MultiPolygon       |


    Scenario: Use 'multipolygon' column for area (splitting multipolygons)
        Given the lua style
            """
            local polygons = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'multipolygon' }
                }
            }

            function osm2pgsql.process_way(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area' }
                })
            end

            function osm2pgsql.process_relation(object)
                polygons:add_row({
                    name = object.tags.name,
                    geom = { create = 'area', split_at = 'multi' }
                })
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_polygon contains exactly
            | osm_id | ST_GeometryType(geom) |
            | 20     | ST_MultiPolygon       |
            | -30    | ST_MultiPolygon       |
            | -31    | ST_MultiPolygon       |
            | -31    | ST_MultiPolygon       |


