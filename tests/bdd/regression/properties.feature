Feature: Updates to the test database with properties check

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

    Scenario Outline: Create/append with various parameters
        When running osm2pgsql pgsql with parameters
            | -c             |
            | <param_create> |

        Given the input file '000466354.osc.gz'
        Then running osm2pgsql pgsql with parameters fails
            | -a             |
            | --slim         |
            | <param_append> |
        And the error output contains
            """
            <message>
            """

        Examples:
            | param_create | param_append   | message                                        |
            |              |                | This database is not updatable                 |
            | --slim       | -x             | because original import was without attributes |
            | --slim       | --prefix=foo   | Different prefix specified                     |
            | --slim       | --flat-nodes=x | Database was imported without flat node file   |


    Scenario Outline: Create/append with various parameters
        When running osm2pgsql pgsql with parameters
            | --slim         |
            | <param_create> |

        Given the input file '000466354.osc.gz'
        When running osm2pgsql pgsql with parameters
            | -a             |
            | --slim         |
            | <param_append> |
        Then the error output contains
            """
            <message>
            """

        Examples:
            | param_create   | param_append   | message                                       |
            | -x             |                | Updating with attributes (same as on import). |
            |                |                | Not using flat node file (same as on import). |
            | --flat-nodes=x |                | Using flat node file                          |
            | --flat-nodes=x | --flat-nodes=x | Using flat node file                          |
            | --flat-nodes=x | --flat-nodes=y | Using the flat node file you specified        |
            | --prefix=abc   |                | Using prefix 'abc' (same as on import).       |

