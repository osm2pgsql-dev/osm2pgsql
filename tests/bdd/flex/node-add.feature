Feature: Adding nodes to a flex database

    Background:
        Given the style file 'test_output_flex_node.lua'

        And the OSM data
            """
            n11 v1 dV Tt1=yes x1 y1
            n12 v1 dV Tt2=yes x2 y2
            n13 v1 dV Ttboth=yes x3 y3
            n14 v1 dV Ttboth=yes x4 y4
            r30 v1 dV Tt=ag Mn11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id |
            | 13     |
            | 14     |


    Scenario: node is not relevant
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Tt=ag x0 y0
            r30 v2 dV Tt=ag Mn10@,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id |
            | 13     |
            | 14     |


    Scenario: add to t1
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Tt1=yes x0 y0
            r30 v2 dV Tt=ag Mn10@,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id |
            | 13     |
            | 14     |


    Scenario: add to t2
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Tt2=yes x0 y0
            r30 v2 dV Tt=ag Mn10@mark,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id | rel_ids |
            | 10     | {30}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id |
            | 13     |
            | 14     |


    Scenario: add to t1 and t2
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Tt1=yes,t2=yes x0 y0
            r30 v2 dV Tt=ag Mn10@mark,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id | rel_ids |
            | 10     | {30}    |
            | 12     | {30}    |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id |
            | 13     |
            | 14     |


    Scenario: add to tboth (only stage1)
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Ttboth=yes x0 y0
            r30 v2 dV Tt=ag Mn10@,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id | rel_ids |
            | 10     | NULL    |
            | 13     | NULL    |
            | 14     | {30}    |


    Scenario: add to tboth (stage1 and stage2)
        Given an empty grid
        And the OSM data
            """
            n10 v1 dV Ttboth=yes x0 y0
            r30 v2 dV Tt=ag Mn10@mark,n11@,n12@mark,n13@,n14@mark
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | node_id |
            | 11     |
        Then table osm2pgsql_test_t2 contains exactly
            | node_id |
            | 12     |
        Then table osm2pgsql_test_tboth contains exactly
            | node_id | rel_ids |
            | 10     | {30}    |
            | 13     | NULL    |
            | 14     | {30}    |
