Feature: Changes on way with expire on zoom 0

    Background:
        Given the style file 'test_expire.lua'

        And the 0.1 grid
            | 11 | 13 |
            | 10 | 12 |

        And the OSM data
            """
            w11 v1 dV Tt1=yes Nn12,n13
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |


    Scenario: way is not relevant
        Given the OSM data
            """
            w10 v1 dV Ta=b Nn10,n11
            """
        And an empty grid

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |


    Scenario: node is not relevant
        Given the OSM data
            """
            n1 v2 dV x1 y2
            """
        And an empty grid

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 11     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |


    Scenario: add to t1
        Given the OSM data
            """
            w10 v1 dV Tt1=yes Nn10,n11
            """
        And an empty grid

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
            | 10     |
            | 11     |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
            | 0    | 0 | 0 |


    Scenario: change in t1
        Given the OSM data
            """
            w11 v2 dV Ta=b Nn10,n11
            """
        And an empty grid

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
            | 0    | 0 | 0 |


    Scenario: remove from t1
        Given the OSM data
            """
            w11 v2 dD
            """
        And an empty grid

        When running osm2pgsql flex with parameters
            | --slim | -a |

        Then table osm2pgsql_test_t1 contains exactly
            | way_id |
        Then table osm2pgsql_test_expire contains exactly
            | zoom | x | y |
            | 0    | 0 | 0 |
