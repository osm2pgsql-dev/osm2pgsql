Feature: Deleting relations in a stage-2 flex database

    Background:
        Given the style file 'test_output_flex_way.lua'

        Given the 0.1 grid
            | 11 | 13 | 15 | 17 | 19 |
            | 10 | 12 | 14 | 16 | 18 |


    Scenario: delete relation with way not in relation
        Given the OSM data
            """
            w10 v1 dV Tt=ag Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r32 v1 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@
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
            r32 v2 dD
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


    Scenario: delete relation with way in t1
        Given the OSM data
            """
            w10 v1 dV Tt1=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r32 v1 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@
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
            r32 v2 dD
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


    Scenario Outline: delete relation with way in t2 (multi)
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim |
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

        Given the OSM data
            """
            <input>
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
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

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario Outline: delete relation with way in t2 (single)
        Given the OSM data
            """
            w10 v1 dV Tt2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {32} |
            | 12     | {30}    |
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
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario Outline: delete relation with way in t1 + t2 (multi)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim |
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
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {31}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario Outline: delete relation with way in t1 + t2 (single)
        Given the OSM data
            """
            w10 v1 dV Tt1=yes,t2=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 10     | {32} |
            | 12     | {30}    |
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
        Then table osm2pgsql_test_t2 contains exactly
            | way_id | rel_ids |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | way_id |
            | 13     |
            | 14     |

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario Outline: delete relation with way in tboth (multi)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r31 v1 dV Tt=ag Mw10@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
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
            | 10     | {31,32} |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            <input>
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

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |


    Scenario Outline: delete relation with way in tboth (single)
        Given the OSM data
            """
            w10 v1 dV Ttboth=yes Nn10,n11
            w11 v1 dV Tt1=yes Nn12,n13
            w12 v1 dV Tt2=yes Nn14,n15
            w13 v1 dV Ttboth=yes Nn16,n17
            w14 v1 dV Ttboth=yes Nn18,n19
            w15 v1 dV Tt=ag Nn17,n19
            r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark
            r32 v1 dV Tt=ag Mw10@mark,w11@,w12@,w13@,w14@,w15@
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
            | 10     | {32}    |
            | 13     | NULL    |
            | 14     | {30}    |

        Given the OSM data
            """
            <input>
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

        Examples:
            | input                                     |
            | r32 v2 dD                                 |
            | r32 v2 dV Tt=ag Mw11@,w12@,w13@,w14@,w15@ |
