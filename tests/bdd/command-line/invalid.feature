Feature: Errors for invalid command line parameter combinations

    Background:
        Given the OSM data
            """
            n1 Tamenity=restaurant,name=Home x=23 y5.6
            """

    Scenario: create and append cannot be used together
        When running osm2pgsql pgsql with parameters
            | -c | -a |
        Then execution fails
        And the error output contains
            """
            --append and --create options can not be used at the same time
            """

    Scenario: append can only be used with slim mode
        When running osm2pgsql pgsql with parameters
            | -a |
        Then execution fails
        And the error output contains
            """
            --append can only be used with slim mode
            """

