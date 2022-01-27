Feature: Tests for Lua geometry processing functions

    Scenario:
        Given the OSM data
            """
            n1 Tamenity=restaurant,name=point x1.1 y1.2
            """
        And the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom4326', type = 'point', projection = 4326 },
                { column = 'geom3857', type = 'point', projection = 3857 },
                { column = 'geomauto', type = 'point', projection = 3857 },
            })

            function osm2pgsql.process_node(object)
                points:insert({
                    name = object.tags.name,
                    geom4326 = object:as_point(),
                    geom3857 = object:as_point():transform(3857),
                    geomauto = object:as_point()
                })
            end

            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_points contains
            | node_id | name  | ST_AsText(geom4326) | geom3857 = geomauto |
            | 1       | point | 1.1 1.2             | True                |

    Scenario:
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
            local tables = {}

            tables.ways = osm2pgsql.define_way_table('osm2pgsql_test_ways', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'linestring', projection = 4326 },
                { column = 'geomsimple', type = 'linestring', projection = 4326 },
            })

            tables.polygons = osm2pgsql.define_area_table('osm2pgsql_test_polygons', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'geometry', projection = 4326 },
                { column = 'center', type = 'point', projection = 4326 },
            })

            function is_empty(some_table)
                return next(some_table) == nil
            end

            function osm2pgsql.process_way(object)
                if is_empty(object.tags) then
                    return
                end

                tables.ways:insert({
                    name = object.tags.name,
                    geom = object:as_linestring(),
                    geomsimple = object:as_linestring():simplify(0.1)
                })
                tables.polygons:insert({
                    name = object.tags.name,
                    geom = object:as_polygon(),
                    center = object:as_polygon():centroid()
                })
            end

            function osm2pgsql.process_relation(object)
                tables.polygons:insert({
                    name = object.tags.name,
                    geom = object:as_multipolygon()
                })
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_ways contains
            | way_id | name  | ST_AsText(geom) | ST_AsText(geomsimple) |
            | 1      | poly  | 1, 2, 4, 3, 1   | 1, 4, 1               |
        And table osm2pgsql_test_polygons contains
            | area_id | name  | ST_AsText(geom)                    | ST_AsText(center) |
            | 1       | poly  | (1, 2, 4, 3, 1)                    | 9.05 50.05        |
            | -1      | multi | (5, 6, 8, 7, 5),(9, 11, 12, 10, 9) | NULL              |

