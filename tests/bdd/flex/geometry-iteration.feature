Feature: Tests for iterating over a (multi)geometry

    Scenario:
        Given the grid
            |  1 |  2 |    |
            |  4 |    |  3 |
            |    |  5 |  6 |
        And the OSM data
            """
            w20 Thighway=motorway Nn1,n2,n3
            w21 Thighway=motorway Nn4,n5,n6
            r30 Ttype=route,route=road Mw20@,w21@
            r31 Ttype=route,route=road Mw20@
            r32 Ttype=something Mw20@
            r33 Ttype=route,route=road Mn1@
            """
        And the lua style
            """
            local routes = osm2pgsql.define_relation_table('osm2pgsql_test_routes', {
                { column = 'geom', type = 'linestring', projection = 4326 },
                { column = 'num_all', type = 'int' },
                { column = 'num_one', type = 'int' }
            })

            local firsts = osm2pgsql.define_relation_table('osm2pgsql_test_firsts', {
                { column = 'geom', type = 'linestring', projection = 4326 },
            })

            function osm2pgsql.process_relation(object)
                if object.tags.type == 'route' then
                    local mgeom = object:as_multilinestring()
                    for sgeom in mgeom:geometries() do
                        routes:insert({
                            geom = sgeom,
                            num_all = #mgeom,
                            num_one = #sgeom
                        })
                    end
                    firsts:insert({
                        geom = mgeom:geometry_n(1), -- first geometry only
                    })
                end
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_routes contains exactly
            | relation_id | ST_AsText(geom) | num_all | num_one |
            | 30          | 1, 2, 3         | 2       | 1       |
            | 30          | 4, 5, 6         | 2       | 1       |
            | 31          | 1, 2, 3         | 1       | 1       |
        And table osm2pgsql_test_firsts contains exactly
            | relation_id | ST_AsText(geom) |
            | 30          | 1, 2, 3         |
            | 31          | 1, 2, 3         |
            | 33          | NULL            |

