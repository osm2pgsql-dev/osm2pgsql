Feature: Changing ways in a flex database

    Background:
        Given the style file 'test_output_flex_way.lua'

        Given the 0.1 grid with origin 10.0 10.1
            | 11 | 13 | 15 | 17 | 19 |
            | 10 | 12 | 14 | 16 | 18 |

    Scenario Outline:
        Given the OSM data:
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w10 v1 dV Tt1=yes Nn10,n11
            r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            <input>
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains
            | way_id |
            | 11     |
        And table osm2pgsql_test_t1 has <num_w10> rows with condition
            """
            way_id = 10
            """
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 10     |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                             | num_w10 |
            | w10 v1 dV Tt2=yes Nn10,n11        | 0       |
            | w10 v1 dV Tt1=yes,t2=yes Nn10,n11 | 1       |


    Scenario Outline: change way from t2
        Given the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w10 v1 dV Tt2=yes Nn10,n11
            r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 10     |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            <input>
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains
            | way_id |
            | 12     |
        And table osm2pgsql_test_t2 has <num_w10> rows with condition
            """
            way_id = 10
            """
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                             | num_w10 |
            | w10 v1 dV Tt1=yes Nn10,n11        | 0       |
            | w10 v1 dV Tt1=yes,t2=yes Nn10,n11 | 1       |


    Scenario Outline: change way from t1 and t2
        Given the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 10     |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            <input>
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains
            | way_id |
            | 11     |
        And table osm2pgsql_test_t1 has <num_t1> rows with condition
            """
            way_id = 10
            """
        Then table osm2pgsql_test_t2 contains
            | way_id |
            | 12     |
        And table osm2pgsql_test_t2 has <num_t2> rows with condition
            """
            way_id = 10
            """
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                      | num_t1 | num_t2 |
            | w10 v1 dV Tt1=yes Nn10,n11 | 1      | 0      |
            | w10 v1 dV Tt2=yes Nn10,n11 | 0      | 1      |


    Scenario Outline: change valid geom to invalid geom
        Given the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w10 v1 dV Tt1=yes,t2=yes,tboth=yes Nn10,n11
            r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            <input>
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                                   |
            | w10 v2 dV Tt1=yes,t2=yes,tboth=yes Nn10 |
            | n11 v2 dV x10.0 y10.0                   |


    Scenario: change invalid geom to valid geom
        Given the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w10 v1 dV Tt1=yes,t2=yes,tboth=yes Nn10
            r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id | rel_ids |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            w10 v2 dV Tt1=yes,t2=yes,tboth=yes Nn10,n11
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 13     | NULL    |
            | 14     | {30}    |
