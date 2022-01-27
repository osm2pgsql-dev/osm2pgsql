Feature: Tests for basic Lua functions

    Scenario:
        Given the OSM data
            """
            n1 Tamenity=restaurant x10 y10
            """
        And the lua style
            """
            this-is-not-valid-lua
            """
        Then running osm2pgsql flex fails
