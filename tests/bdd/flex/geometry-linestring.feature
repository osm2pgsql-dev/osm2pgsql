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
            | way_id | sgeom!geo | mgeom!geo   | xgeom!geo   |
            | 20     | 1, 2, 3   | [ 1, 2, 3 ] | [ 1, 2, 3 ] |
            | 21     | 4, 5      | [ 4, 5 ]    | [ 4, 5 ]    |

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
        When running osm2pgsql flex
        Then execution fails

        And the error output contains
            """
            Geometry data for geometry column 'geom' has the wrong type (LINESTRING).
            """

    Scenario:
        Given the grid
            | 1 | 2 |   |
            |   |   | 3 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2,n3,n1
            w21 Thighway=motorway Nn1,n2,n2,n3,n1
            w22 Thighway=motorway Nn1,n2,n3,n1,n1
            w23 Thighway=motorway Nn1,n2,n3
            w24 Thighway=motorway Nn2,n2
            """
        And the lua style
            """
            local lines = osm2pgsql.define_way_table('osm2pgsql_test_lines', {
                { column = 'lgeom', type = 'linestring', projection = 4326 },
                { column = 'pgeom', type = 'polygon', projection = 4326 },
                { column = 'lclean', type = 'bool' },
                { column = 'pclean', type = 'bool' },
            })

            function osm2pgsql.process_way(object)
                lgeom, lclean = object:as_linestring()
                pgeom, pclean = object:as_polygon()
                lines:insert({
                    lgeom = lgeom,
                    pgeom = pgeom,
                    lclean = lclean,
                    pclean = pclean,
                })
            end

            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_lines contains exactly
            | way_id | lgeom!geo  | lclean | pgeom!geo    | pclean |
            | 20     | 1, 2, 3, 1 | True   | (1, 2, 3, 1) | True   |
            | 21     | 1, 2, 3, 1 | False  | (1, 2, 3, 1) | False  |
            | 22     | 1, 2, 3, 1 | False  | (1, 2, 3, 1) | False  |
            | 23     | 1, 2, 3    | True   | NULL         | NULL   |
            | 24     | NULL       | NULL   | NULL         | NULL   |

