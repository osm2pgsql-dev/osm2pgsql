Feature: Tests for various DB connection parameters

    Scenario Outline:
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """

        When running osm2pgsql pgsql with parameters
            | -d |
            | <connection_parameter> |

        Then table planet_osm_point has 1 row

        Examples:
            | connection_parameter    |
            | {TEST_DB}               |
            | dbname={TEST_DB}        |
            | postgresql:///{TEST_DB} |
            | postgres:///{TEST_DB}   |
