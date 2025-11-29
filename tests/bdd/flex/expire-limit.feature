Feature: Changes with limited expire on zoom 2

    Background:
        Given the style file 'test_expire_limit.lua'

        And the OSM data
            """
            n10 v1 dV x10 y10
            n11 v1 dV x100 y10
            n12 v1 dV x10 y70
            n13 v1 dV x100 y70
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |


    Scenario: short ways are okay
        Given the OSM data
            """
            w20 v1 dV Ta=b Nn10,n11
            w21 v1 dV Ta=b Nn10,n12
            """

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 20     |
            | 21     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
            | 2    | 2 | 1 |
            | 2    | 3 | 1 |
            | 2    | 2 | 0 |


    Scenario: long way is not okay
        Given the OSM data
            """
            w20 v1 dV Ta=b Nn10,n11,n13
            """

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 20     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
        And the error output contains
            """
            Tile limit 2 reached for single geometry!
            """

    Scenario: too many tiles overall is not okay
        Given the OSM data
            """
            n14 v1 dV x100 y-10
            n15 v1 dV x100 y-70
            n16 v1 dV x10 y-70
            n17 v1 dV x-10 y-70
            n18 v1 dV x-100 y-70
            w20 v1 dV Ta=b Nn13,n11
            w21 v1 dV Ta=b Nn14,n15
            w22 v1 dV Ta=b Nn15,n16
            w23 v1 dV Ta=b Nn16,n17
            w24 v1 dV Ta=b Nn17,n18
            """

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 20     |
            | 21     |
            | 22     |
            | 23     |
            | 24     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
            | 2    | 3 | 0 |
            | 2    | 3 | 1 |
            | 2    | 3 | 2 |
            | 2    | 2 | 3 |
            | 2    | 3 | 3 |
        And the error output contains
            """
            Overall tile limit 6 reached for this run!
            """

