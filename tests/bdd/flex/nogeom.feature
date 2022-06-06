Feature: Handling of tables without geometry

    Scenario: Updating table without geometry should work
        Given the OSM data
            """
            n10 v1 dV Tamenity=restaurant x10.0 y10.0
            n11 v1 dV Tamenity=post_box x10.0 y10.2
            """
        And the lua style
            """
            local pois = osm2pgsql.define_node_table('osm2pgsql_test_pois', {
                { column = 'tags',  type = 'hstore' },
            })

            function osm2pgsql.process_node(object)
                pois:add_row{ tags = object.tags }
            end
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_pois contains exactly
            | node_id | tags->'name' |
            | 10      | NULL         |
            | 11      | NULL         |

        Given the OSM data
            """
            n10 v2 dV Tamenity=restaurant,name=Schwanen x10.0 y10.0
            """

        When running osm2pgsql flex with parameters
            | --slim | --append |

        Then table osm2pgsql_test_pois contains exactly
            | node_id | tags->'name' |
            | 10      | Schwanen     |
            | 11      | NULL         |



