Feature: Deleting ways in a 2-stage flex database

    Background:
        Given the style file 'test_output_flex_way.lua'

        Given the 0.1 grid
            | 11 | 13 | 15 | 17 | 19 |
            | 10 | 12 | 14 | 16 | 18 |


    Scenario: delete way which is not a member and not in tables
        Given the OSM data
            """
            w10 v1 dV Tt=ag Nn10,n11
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

        Given the OSM data
            """
            w10 v2 dD
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


    Scenario: delete way which is not a member and in t1 table
        Given the OSM data
            """
            w10 v1 dV Tt1=yes Nn10,n11
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
            w10 v2 dD
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


    Scenario: delete way which is not a member and in tboth table
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
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
            | 10     |
            | 13     |
            | 14     |

        Given the OSM data
            """
            w10 v2 dD
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


    Scenario: delete way which is a member and not in tables
        Given the OSM data
            """
            w10 v1 dV Tt=ag Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
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

        Given the OSM data
            """
            w10 v2 dD
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


    Scenario: delete way which is a member and in t1 table
        Given the OSM data
            """
            w10 v1 dV Tt1=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
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
            w10 v2 dD
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


    Scenario: delete way which is a member and in t2 table
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
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
            w10 v2 dD
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


    Scenario: delete way which is a member and in t1+t2 tables
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
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
            w10 v2 dD
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


    Scenario Outline: delete way which is a member and in tboth table
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            r30 v1 dV Tt=ag Mw10@<role>,w11@,w12@mark,w13@,w14@mark
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
            | 10     |
            | 13     |
            | 14     |

        Given the OSM data
            """
            w10 v2 dD
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


        Examples:
            | role |
            | mark |
            |      |
