Feature: Flex output uses a Lua config file

    Scenario: Check access to osm2pgsql object from Lua
        Given the OSM data
            """
            """
        And the lua style
            """
            print("version=" .. osm2pgsql.version)
            print("mode=" .. osm2pgsql.mode)
            print("stage=" .. osm2pgsql.stage)
            print("Table=" .. type(osm2pgsql.Table))
            """
        When running osm2pgsql flex
        Then the error output contains
            """
            No output tables defined
            """
        And the standard output contains
            """
            mode=create
            """
        And the standard output contains
            """
            stage=1
            """
        And the standard output contains
            """
            Table=table
            """

    Scenario: Check access to osm2pgsql properties from Lua
        Given the OSM data
            """
            """
        And the lua style
            """
            local p = osm2pgsql.properties
            print("attributes=" .. p.attributes)
            print("prefix=" .. p.prefix)
            """
        When running osm2pgsql flex
        Then the standard output contains
            """
            attributes=false
            """
        And the standard output contains
            """
            prefix=planet_osm
            """

