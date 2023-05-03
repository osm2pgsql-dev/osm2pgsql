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

    Scenario: append can only be used with slim mode
        Then running osm2pgsql pgsql with parameters fails
            | -a |
        And the error output contains
            """
            --append can only be used with slim mode
            """

    Scenario: append and middle-database-format cannot be used together
        Then running osm2pgsql pgsql with parameters fails
            | -a | --slim | --middle-database-format=new |
        And the error output contains
            """
            Do not use --middle-database-format with --append.
            """

    Scenario: middle-database-format value
        Then running osm2pgsql pgsql with parameters fails
            | --slim | --middle-database-format=foo |
        And the error output contains
            """
            Unknown value for --middle-database-format
            """

