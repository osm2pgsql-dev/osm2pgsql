Feature: Adding relations to a 2-stage flex database

    Background:
        Given the style file 'test_output_flex_way.lua'

        Given the 0.1 grid
            | 11 | 13 | 15 | 17 | 19 |
            | 10 | 12 | 14 | 16 | 18 |


    Scenario Outline: add relation with way in t1 (marked)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            <extrarel>
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
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {32}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | extrarel                                  |
            |                                           |
            | r32 v1 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario: add relation with way in t2 (marked)
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31,32} |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: add relation with way in t1 and t2 (marked)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31,32} |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario Outline: add (to) relation with way in tboth stage 1 (marked)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            <extrarel>
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
            | way_id | rel_ids |
            | 10     | NULL    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
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
            | 10     | {32}    |
            | 13     | NULL    |
            | 14     | {30}    |

        Examples:
            | extrarel                                       |
            | r31 v1 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@      |
            | r32 v1 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@,w15@ |


    Scenario: add relation with way in tboth stage 2 (marked)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
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
            | way_id | rel_ids |
            | 10     | {31}    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
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
            | 10     | {31,32} |
            | 13     | NULL    |
            | 14     | {30}    |


    Scenario Outline: add relation with way in t1 (not marked)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            <extrarel>
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
            r32 v2 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@,w15@
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

        Examples:
            | extrarel                                  |
            |                                           |
            | r32 v1 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario: add relation with way in t2 (not marked)
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31,32} |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: add relation with way in t1 and t2 (not marked)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |


    Scenario: add relation with way in tboth stage 1 (not marked)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@
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
            | way_id | rel_ids |
            | 10     | NULL    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@,w15@
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


    Scenario: add relation with way in tboth stage 2 (not marked)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@
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
            | way_id | rel_ids |
            | 10     | {31}    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            r32 v2 dV Tt=ag Mw10@,w11@,w12@,w13@,w14@,w15@
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
            | 10     | {31}    |
            | 13     | NULL    |
            | 14     | {30}    |
