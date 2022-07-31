Feature: Flex output uses a Lua config file

    Scenario: Check access to osm2pgsql object from Lua
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            print("version=" .. osm2pgsql.version)
            print("mode=" .. osm2pgsql.mode)
            print("stage=" .. osm2pgsql.stage)
            print("Table=" .. type(osm2pgsql.Table))
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            No tables defined in Lua config. Nothing to do!
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
