Feature: Tests for geometry split_multi function

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
            })

            function osm2pgsql.process_relation(object)
                if object.tags.type == 'route' then
                    local g = object:as_multilinestring()
                    if not g:is_null() then
                        for n, line in pairs(g:split_multi()) do
                            routes:insert({
                                geom = line
                            })
                        end
                    end
                end
            end

            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_routes contains exactly
            | relation_id | ST_AsText(geom) |
            | 30          | 1, 2, 3         |
            | 30          | 4, 5, 6         |
            | 31          | 1, 2, 3         |
