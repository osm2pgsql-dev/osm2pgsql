Feature: Tests for area column type

    Scenario Outline:
        Given the 0.1 grid with origin 9.0 50.3
            |    |    |  7 |    |    |  8 |
            |    |    |    | 11 | 12 |    |
            |  3 |  4 |    |  9 | 10 |    |
            |  1 |  2 |  5 |    |    |  6 |
        And the OSM data
            """
            w1 Tnatural=water,name=poly Nn1,n2,n4,n3,n1
            w2 Nn5,n6,n8,n7,n5
            w3 Nn9,n10,n12,n11,n9
            r1 Tnatural=water,name=multi Mw2@,w3@
            """
        And the lua style
            """
            local polygons = osm2pgsql.define_table{
                name = 'osm2pgsql_test_polygon',
                ids = { type = 'area', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'geometry', projection = <geom proj> },
                    { column = 'area', type = 'area', projection = <area proj> },
                }
            }

            function is_empty(some_table)
                return next(some_table) == nil
            end

            function osm2pgsql.process_way(object)
                if is_empty(object.tags) then
                    return
                end

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
        Then table osm2pgsql_test_polygon contains
            | name  | ST_Area(geom)  | area         | ST_Area(ST_Transform(geom, 4326)) |
            | poly  | <st_area poly> | <area poly>  | 0.01 |
            | multi | <st_area multi>| <area multi> | 0.08 |

        Examples:
            | geom proj | area proj | st_area poly | area poly    | st_area multi | area multi    |
            | 4326      | 4326      | 0.01         | 0.01         | 0.08          | 0.08          |
            | 4326      | 3857      | 0.01         | 192987010.0  | 0.08          | 1547130000.0  |
            | 3857      | 4326      | 192987010.0  | 0.01         | 1547130000.0  | 0.08          |
            | 3857      | 3857      | 192987010.0  | 192987010.0  | 1547130000.0  | 1547130000.0  |

        @config.have_proj
        Examples: Generic projection
            | geom proj | area proj | st_area poly | area poly    | st_area multi | area multi    |
            | 4326      | 25832     | 0.01         | 79600737.537 | 0.08          | 635499542.954 |
            | 3857      | 25832     | 192987010.0  | 79600737.537 | 1547130000.0  | 635499542.954 |
            | 25832     | 4326      | 79600737.537 | 0.01         | 635499542.954 | 0.08          |
            | 25832     | 3857      | 79600737.537 | 192987010.0  | 635499542.954 | 1547130000.0  |
            | 25832     | 25832     | 79600737.537 | 79600737.537 | 635499542.954 | 635499542.954 |

