Feature: Lua callbacks are called

    Scenario: Check access to osm2pgsql object from Lua
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local nodes = 0
            local ways = 0
            local relations = 0
            local n = 0

            osm2pgsql.define_node_table('dummy', {})

            function osm2pgsql.process_node(object)
                nodes = nodes + 1
            end

            function osm2pgsql.process_way(object)
                ways = ways + 1
            end

            function osm2pgsql.process_relation(object)
                relations = relations + 1
            end

            local function out()
                print(n .. 'n=' .. nodes .. '@')
                print(n .. 'w=' .. ways .. '@')
                print(n .. 'r=' .. relations .. '@')
                n = n + 1
            end

            osm2pgsql.after_nodes = out
            osm2pgsql.after_ways = out
            osm2pgsql.after_relations = out
            """
        When running osm2pgsql flex
        Then the standard output contains
            """
            0n=1562@
            0w=0@
            0r=0@
            1n=1562@
            1w=7105@
            1r=0@
            2n=1562@
            2w=7105@
            2r=113@
            """
