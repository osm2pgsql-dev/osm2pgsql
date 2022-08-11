Feature: Creating linestring features from way

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
            local lines = osm2pgsql.define_way_table('osm2pgsql_test_lines', {
                { column = 'sgeom', type = 'linestring', projection = 4326 },
                { column = 'mgeom', type = 'multilinestring', projection = 4326 },
                { column = 'xgeom', type = 'multilinestring', projection = 4326 },
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway == 'motorway' then
                    lines:insert({
                        sgeom = object:as_linestring(),
                        mgeom = object:as_multilinestring(),
                        xgeom = object:as_linestring()
                    })
                end
            end

            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_lines contains exactly
            | way_id | ST_AsText(sgeom) | ST_AsText(ST_GeometryN(mgeom, 1)) | ST_AsText(ST_GeometryN(xgeom, 1)) |
            | 20     | 1, 2, 3          | 1, 2, 3                           | 1, 2, 3                           |
            | 21     | 4, 5             | 4, 5                              | 4, 5                              |

    Scenario:
        Given the grid
            | 1 | 2 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2
            """
        And the lua style
            """
            local lines = osm2pgsql.define_way_table('osm2pgsql_test_lines', {
                { column = 'geom', type = 'polygon', projection = 4326 },
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway == 'motorway' then
                    lines:insert({
                        geom = object:as_linestring(),
                    })
                end
            end

            """
        Then running osm2pgsql flex fails

        And the error output contains
            """
            Geometry data for geometry column 'geom' has the wrong type (LINESTRING).
            """

