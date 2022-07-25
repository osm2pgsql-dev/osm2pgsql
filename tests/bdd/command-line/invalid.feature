Feature: Errors for invalid command line parameter combinations

    Background:
        Given the OSM data
            """
            n1 Tamenity=restaurant,name=Home x=23 y5.6
            """

    Scenario: create and append cannot be used together
        Then running osm2pgsql pgsql with parameters fails
            | -c | -a |
        And the error output contains
            """
            --append and --create options can not be used at the same time
            """

