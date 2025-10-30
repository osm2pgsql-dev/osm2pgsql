Feature: Creating point features from way

    Scenario:
        Given the grid
            | 1 | 2 |   |
            | 4 |   | 3 |
            |   | 5 |   |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2,n3
            w21 Thighway=motorway Nn4,n5
            """
        And the lua style
            """
            local points = osm2pgsql.define_way_table('osm2pgsql_test_points', {
                { column = 'n', type = 'int' },
                { column = 'geom', type = 'point', projection = 4326, not_null = false },
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway == 'motorway' then
                    points:insert({ n = nil, geom = object:as_point() })
                    points:insert({ n =   0, geom = object:as_point(0) })
                    points:insert({ n =   1, geom = object:as_point(1) })
                    points:insert({ n =   2, geom = object:as_point(2) })
                    points:insert({ n =   3, geom = object:as_point(3) })
                    points:insert({ n =   4, geom = object:as_point(4) })
                    points:insert({ n =  -1, geom = object:as_point(-1) })
                    points:insert({ n =  -2, geom = object:as_point(-2) })
                    points:insert({ n =  -3, geom = object:as_point(-3) })
                end
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_points contains exactly
            | way_id | n    | ST_AsText(geom) |
            | 20     | NULL | 1               |
            | 20     | 0    | NULL            |
            | 20     | 1    | 1               |
            | 20     | 2    | 2               |
            | 20     | 3    | 3               |
            | 20     | 4    | NULL            |
            | 20     | -1   | 3               |
            | 20     | -2   | 2               |
            | 20     | -3   | 1               |
            | 21     | NULL | 4               |
            | 21     | 0    | NULL            |
            | 21     | 1    | 4               |
            | 21     | 2    | 5               |
            | 21     | 3    | NULL            |
            | 21     | 4    | NULL            |
            | 21     | -1   | 5               |
            | 21     | -2   | 4               |
            | 21     | -3   | NULL            |

    Scenario:
        Given the grid
            | 1 | 2 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2
            """
        And the lua style
            """
            function osm2pgsql.process_way(object)
                local geom = object:as_point('foo')
            end
            """
        Then running osm2pgsql flex fails

        And the error output contains
            """
            Argument #1 to 'as_point()' must be an integer.
            """

    Scenario:
        Given the grid
            | 1 | 2 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2
            """
        And the lua style
            """
            function osm2pgsql.process_way(object)
                local geom = object:as_point(1, 'foo')
            end
            """
        Then running osm2pgsql flex fails

        And the error output contains
            """
            Too many arguments for function as_point()
            """
