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
                { column = 'npoints', type = 'int' },
                { column = 'length', type = 'real' },
                { column = 'slength', type = 'real' },
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway == 'motorway' then
                    lines:insert({
                        sgeom = object:as_linestring(),
                        mgeom = object:as_multilinestring(),
                        xgeom = object:as_linestring(),
                        npoints = object:as_linestring():n_points(),
                        length = object:as_linestring():length(),
                        slength = object:as_linestring():spherical_length(),
                    })
                end
            end

            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_lines contains exactly
            | way_id | sgeom!geo | mgeom!geo   | xgeom!geo   | npoints | length     | slength   |
            | 20     | 1, 2, 3   | [ 1, 2, 3 ] | [ 1, 2, 3 ] | 3       | 0.24142136 | 25718.176 |
            | 21     | 4, 5      | [ 4, 5 ]    | [ 4, 5 ]    | 2       | 0.14142136 | 15235.885 |

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
            | 1 | 2 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n1,n2
            """
        And the lua style
            """
            local points = osm2pgsql.define_way_table('osm2pgsql_test', {
                { column = 'geom', type = 'point', projection = 4326 },
                { column = 'dupl', type = 'boolean' },
            })

            function osm2pgsql.process_way(object)
                if #object.nodes > 1 then
                    local prev = object:as_point(1)
                    points:insert({ geom = prev, dupl = false })
                    for n = 2, #object.nodes do
                        local geom = object:as_point(n)
                        points:insert({ geom = geom, dupl = (prev == geom) })
                    end
                end
            end

            """
        When running osm2pgsql flex

        Then table osm2pgsql_test contains exactly
            | way_id | geom!geo | dupl  |
            | 20     | 1        | False |
            | 20     | 1        | True  |
            | 20     | 2        | False |

