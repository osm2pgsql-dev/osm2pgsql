Feature: Adding untagged objects to a flex database

    Scenario: Import with normal and "untagged" callbacks
        Given the style file 'test_output_flex_untagged.lua'
        And the OSM data
            """
            n11 v1 dV x1 y1
            n12 v1 dV x2 y2
            n13 v1 dV x3 y3
            n14 v1 dV Tamenity=restaurant x4 y4
            w20 v1 dV Thighway=primary Nn11,n12
            w21 v1 dV Nn13,n14
            r30 v1 dV Mn11@,w20@
            r31 v1 dV Ttype=route Mw20@
            """
        When running osm2pgsql flex
        Then table osm2pgsql_test_nodes contains exactly
            | node_id | tagged | tags |
            | 11      | False  | {} |
            | 12      | False  | {} |
            | 13      | False  | {} |
            | 14      | True   | {'amenity': 'restaurant'} |

        Then table osm2pgsql_test_ways contains exactly
            | way_id | tagged | tags |
            | 20     | True   | {'highway': 'primary'} |
            | 21     | False  | {} |

        Then table osm2pgsql_test_relations contains exactly
            | relation_id | tagged | tags |
            | 30          | False  | {} |
            | 31          | True   | {'type': 'route'} |

    Scenario: Import then update with normal and "untagged" callbacks
        Given the style file 'test_output_flex_untagged.lua'
        And the OSM data
            """
            n11 v1 dV x1 y1
            n12 v1 dV x2 y2
            n13 v1 dV x3 y3
            n14 v1 dV Tamenity=restaurant x4 y4
            w20 v1 dV Thighway=primary Nn11,n12
            w21 v1 dV Nn13,n14
            w22 v1 dV Nn11,n12
            r30 v1 dV Mn11@,w20@
            r31 v1 dV Ttype=route Mw20@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_nodes contains exactly
            | node_id | tagged | tags |
            | 11      | False  | {} |
            | 12      | False  | {} |
            | 13      | False  | {} |
            | 14      | True   | {'amenity': 'restaurant'} |

        Then table osm2pgsql_test_ways contains exactly
            | way_id | tagged | tags |
            | 20     | True   | {'highway': 'primary'} |
            | 21     | False  | {} |
            | 22     | False  | {} |

        Then table osm2pgsql_test_relations contains exactly
            | relation_id | tagged | tags |
            | 30          | False  | {} |
            | 31          | True   | {'type': 'route'} |

        Given the style file 'test_output_flex_untagged.lua'
        And the OSM data
            """
            n11 v2 dV Tnatural=tree x1 y1
            n14 v2 dV x4 y4
            w21 v2 dV Nn14,n13
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table osm2pgsql_test_nodes contains exactly
            | node_id | tagged | tags |
            | 11      | True   | {'natural': 'tree'} |
            | 12      | False  | {} |
            | 13      | False  | {} |
            | 14      | False  | {} |

        Then table osm2pgsql_test_ways contains exactly
            | way_id | tagged | tags |
            | 20     | True   | {'highway': 'primary'} |
            | 21     | False  | {} |
            | 22     | False  | {} |

        Then table osm2pgsql_test_relations contains exactly
            | relation_id | tagged | tags |
            | 30          | False  | {} |
            | 31          | True   | {'type': 'route'} |

