Feature: Test handling of invalid geometries

    Background:
        Given the lua style
            """
            local tables = {}

            tables.line = osm2pgsql.define_table{
                name = 'osm2pgsql_test_line',
                ids = { type = 'way', id_column = 'osm_id' },
                columns = {
                    { column = 'tags', type = 'hstore' },
                    { column = 'geom', type = 'linestring', projection = 4326 },
                }
            }

            tables.polygon = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'tags', type = 'hstore' },
                    { column = 'geom', type = 'geometry', projection = 4326 }
                }
            }

            function osm2pgsql.process_way(object)
                if object.tags.natural then
                    tables.polygon:add_row({
                        tags = object.tags,
                        geom = { create = 'area' }
                    })
                else
                    tables.line:add_row({
                        tags = object.tags
                    })
                end
            end

            function osm2pgsql.process_relation(object)
                tables.polygon:add_row({
                    tags = object.tags,
                    geom = { create = 'area', split_at = 'multi' }
                })
            end
            """

    Scenario: Invalid way geometry should be ignored
        Given the grid with origin 10.0 10.0
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            n14 v1 dV x10.0 y10.0
            w20 v1 dV Thighway=primary,state=okay Nn10,n12
            w21 v1 dV Thighway=primary,state=unknown_node Nn10,n12,n13
            w22 v1 dV Thighway=primary,state=unknown_node_single_node Nn10,n13
            w23 v1 dV Thighway=primary,state=single_node Nn10
            w24 v1 dV Thighway=primary,state=double_node Nn10,n10
            w25 v1 dV Thighway=primary,state=double_location Nn10,n14
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_polygon has 0 rows
        Then table osm2pgsql_test_line contains exactly
            | osm_id | ST_AsText(geom) |
            | 20     | 10, 12          |
            | 21     | 10, 12          |


    Scenario: Invalid area geometry from way should be ignored
        Given the grid
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            w20 v1 dV Tnatural=wood,state=okay Nn10,n11,n12,n10
            w21 v1 dV Tnatural=wood,state=unknown_node Nn10,n11,n12,n13,n10
            w22 v1 dV Tnatural=wood,state=duplicate_segment Nn10,n11,n12,n10,n11
            w23 v1 dV Tnatural=wood,state=unclosed_ring Nn10,n11,n12
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_line has 0 rows
        Then table osm2pgsql_test_polygon contains exactly
            | osm_id | ST_AsText(geom)  |
            | 20     | (10, 12, 11, 10) |
            | 21     | (10, 12, 11, 10) |


    Scenario: Area with self-intersection from way should be ignored
        Given the grid
           | 10  |  12 |
           | 11  |  13 |
        And the OSM data
            """
            w20 v1 dV Tnatural=wood Nn10,n11,n12,n13,n10
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_line has 0 rows
        Then table osm2pgsql_test_polygon has 0 rows


    Scenario: Invalid area geometry from relation should be ignored
        Given the grid
            | 10 | 11 |
            | 13 | 12 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon,landuse=forest,state=okay Mw20@,w21@
            r31 v1 dV Ttype=multipolygon,landuse=forest,state=not_closed Mw20@
            r32 v1 dV Ttype=multipolygon,landuse=forest,state=missing_way Mw20@,w22@
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_line has 0 rows
        Then table osm2pgsql_test_polygon contains exactly
            | osm_id |
            | -30    |

