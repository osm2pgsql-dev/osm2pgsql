Feature: Locators

    Scenario: Define a locator without parameter
        Given the OSM data
            """
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator()
            print(regions:name())
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Argument #1 to 'define_locator' must be a Lua table.
            """

    Scenario: Define a locator with a name
        Given the OSM data
            """
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'aname' })
            print('NAME[' .. regions:name() .. ']')
            """
        When running osm2pgsql flex
        Then the standard output contains
            """
            NAME[aname]
            """

    Scenario: Define a locator without name is okay
        Given the OSM data
            """
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({})
            print('NAME[' .. regions:name() .. ']')
            """
        When running osm2pgsql flex
        Then the standard output contains
            """
            NAME[]
            """

    Scenario: Calling name() on locator with . instead of : does not work
        Given the OSM data
            """
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'test' })
            print(regions.name())
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Argument #1 has to be of type osm2pgsql.Locator.
            """

    Scenario: Use a first_intersecting() without geometry fails
        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x0.5 y0.5
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_bbox('B1', 0.0, 0.0, 1.0, 1.0)

            function osm2pgsql.process_node(object)
                local r = regions:first_intersecting()
            end
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Error in 'first_intersecting': Need locator and geometry arguments
            """

    Scenario: Use of all_intersecting() without geometry fails
        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x0.5 y0.5
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_bbox('B1', 0.0, 0.0, 1.0, 1.0)

            function osm2pgsql.process_node(object)
                local r = regions:all_intersecting()
            end
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Error in 'all_intersecting': Need locator and geometry arguments
            """

    Scenario: Define and use a locator with first_intersecting
        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x0.5 y0.5
            n11 v1 dV Tamenity=post_box x2.5 y2.5
            n12 v1 dV Tamenity=post_box x1.5 y1.5
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_bbox('B1', 0.0, 0.0, 1.0, 1.0)
            regions:add_bbox('B2', 1.0, 1.0, 2.0, 2.0)

            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'region', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                local g = object:as_point()
                local r = regions:first_intersecting(g)
                if r then
                    points:insert({
                        region = r,
                        geom = g,
                    })
                end
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_points contains exactly
            | node_id | region | ST_AsText(geom) |
            | 10      | B1     | 0.5 0.5         |
            | 12      | B2     | 1.5 1.5         |

    Scenario: Define and use a locator with all_intersecting
        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x0.5 y0.5
            n11 v1 dV Tamenity=post_box x2.5 y2.5
            n12 v1 dV Tamenity=post_box x1.5 y1.5
            n13 v1 dV Tamenity=post_box x1.0 y1.0
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_bbox('B1', 0.0, 0.0, 1.0, 1.0)
            regions:add_bbox('B2', 1.0, 1.0, 2.0, 2.0)

            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'num_regions', type = 'int' },
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                local g = object:as_point()
                local r = regions:all_intersecting(g)
                if #r > 0 then
                    points:insert({
                        num_regions = #r,
                        geom = g,
                    })
                end
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_points contains exactly
            | node_id | num_regions | ST_AsText(geom) |
            | 10      | 1           | 0.5 0.5         |
            | 12      | 1           | 1.5 1.5         |
            | 13      | 2           | 1 1             |

    Scenario: Define and use a locator with polygon from db
        Given the 10.0 grid with origin 10.0 10.0
            | 10 | 11 |
            | 12 |    |
        And the OSM data
            """
            w20 v1 dV Tsome=boundary Nn10,n11,n12,n10
            """
        And the lua style
            """
            local regions = osm2pgsql.define_way_table('osm2pgsql_test_regions', {
                { column = 'region', type = 'text' },
                { column = 'geom', type = 'polygon', projection = 4326 },
            })

            function osm2pgsql.process_way(object)
                regions:insert({
                    region = 'P1',
                    geom = object:as_polygon(),
                })
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_regions contains exactly
            | way_id | region | ST_AsText(geom)         |
            | 20     | P1     | (10 0,20 10,10 10,10 0) |

        Given the OSM data
            """
            n10 v1 dV Tamenity=post_box x15.0 y8.0
            n11 v1 dV Tamenity=post_box x15.0 y2.0
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_from_db('SELECT region, geom FROM osm2pgsql_test_regions')

            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'region', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                local g = object:as_point()
                local r = regions:first_intersecting(g)
                if r then
                    points:insert({
                        region = r,
                        geom = g,
                    })
                end
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_points contains exactly
            | node_id | region | ST_AsText(geom) |
            | 10      | P1     | 15 8            |

    Scenario: Define and use a locator with relation from db
        Given the 10.0 grid with origin 10.0 10.0
            | 10 | 11 | 12 |
            | 13 | 14 | 15 |
        And the OSM data
            """
            w29 v1 dV Tregion=P1 Nn10,n11,n14,n13,n10
            """
        And the lua style
            """
            local regions = osm2pgsql.define_way_table('osm2pgsql_test_regions', {
                { column = 'region', type = 'text' },
                { column = 'geom', type = 'polygon', projection = 4326 },
            })

            function osm2pgsql.process_way(object)
                regions:insert({
                    region = object.tags.region,
                    geom = object:as_polygon(),
                })
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_regions contains exactly
            | way_id | region | ST_AsText(geom)               |
            | 29     | P1     | (10 0,20 0,20 10,10 10, 10 0) |

        Given the 10.0 grid with origin 10.0 10.0
            | 10 | 11 | 12 |
            | 13 | 14 | 15 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n13
            w21 v1 dV Nn13,n10
            w22 v1 dV Nn14,n15
            w23 v1 dV Nn12,n15
            r30 v1 dV Tfoo=bar Mw20@,w21@,w22@,n12@
            r31 v1 dV Tfoo=bar Mn12@,n15@
            r32 v1 dV Tfoo=bar Mw23@
            """
        And the lua style
            """
            local regions = osm2pgsql.define_locator({ name = 'regions' })
            regions:add_from_db('SELECT region, geom FROM osm2pgsql_test_regions')

            local points = osm2pgsql.define_relation_table('osm2pgsql_test_rels', {
                { column = 'region', type = 'text' },
                { column = 'geom', type = 'geometry', projection = 4326 },
            })

            function osm2pgsql.process_relation(object)
                local g = object:as_geometrycollection()
                local r = regions:first_intersecting(g)
                if r then
                    points:insert({
                        region = r,
                        geom = g,
                    })
                end
            end
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_rels contains exactly
            | relation_id | region | ST_GeometryType(geom) |
            | 30          | P1     | ST_GeometryCollection |

