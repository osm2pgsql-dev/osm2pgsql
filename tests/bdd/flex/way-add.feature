Feature: Adding ways to a flex database

    Background:
        Given the style file 'test_output_flex_way.lua'

        Given the 0.1 grid
            | 11 | 13 | 15 | 17 | 19 |
            | 10 | 12 | 14 | 16 | 18 |

        And the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            r30 v1 dV Tt=ag Mw11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: way is not relevant
        Given the OSM data
            """
            w10 v1 dV Tt=ag Nn10,n11
            r30 v2 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: add to t1
        Given the OSM data
            """
            w10 v1 dV Tt1=yes Nn10,n11
            r30 v2 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

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


    Scenario: add to t2
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            r30 v2 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: add to t1 and t2
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            r30 v2 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
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
            | way_id |
            | 13     |
            | 14     |


    Scenario: add to tboth (only stage1)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            r30 v2 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id | rel_ids |
            | 10     | NULL    |
            | 13     | NULL    |
            | 14     | {30}    |


    Scenario: add to tboth (stage1 and stage2)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            r30 v2 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id | rel_ids |
            | 10     | {30}    |
            | 13     | NULL    |
            | 14     | {30}    |
