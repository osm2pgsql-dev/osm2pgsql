Feature: Handling changes to relations

    Background:
        Given the lua style
            """
            local rel_table = osm2pgsql.define_area_table('osm2pgsql_test_relations', {
                { column = 'tags', type = 'hstore' },
                { column = 'geom', type = 'geometry' }
            })

            function osm2pgsql.process_relation(object)
                if object.tags.type == 'multipolygon' then
                    rel_table:add_row{
                        tags = object.tags,
                        geom = { create = 'area' }
                    }
                end
            end
            """

    Scenario: Changing type adds relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 0 rows

        Given the OSM data
            """
            r30 v2 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has 1 row


    Scenario: Changing way adds relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 0 rows

        Given the OSM data
            """
            w21 v2 dV Nn12,n13,n10
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has 1 row


    Scenario: Changing node adds relation
        Given the 0.1 grid with origin 10.0 10.0
            | 10 | 11 | 12 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n10
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 0 rows

        Given the OSM data
            """
            n12 v2 dV x10.1 y10.1
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has 1 row


    Scenario: Changing memberlist adds relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon Mw20@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 0 rows

        Given the OSM data
            """
            r30 v2 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has 1 row


    Scenario: Changing type deletes relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 1 row

        Given the OSM data
            """
            r30 v2 dV Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has 0 rows


    Scenario Outline: Changing ways in valid relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 1 row

        Given the OSM data
            """
            w21 v2 dV <new nodelist>
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has <expected rows> rows

        Examples:
            | new nodelist | expected rows |
            | Nn12,n13     | 0             |
            | Nn10,n13,n12 | 1             |


    Scenario Outline: Changing nodes in a valid relation
        Given the 0.1 grid with origin 10.0 10.0
            | 10 | 11 |
            |    | 12 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n10
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 1 row

        Given the OSM data
            """
            n12 v2 dV <new coordinates>
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has <expected rows> rows

        Examples:
            | new coordinates | expected rows |
            | x10.1 y10.0     | 0             |
            | x10.05 y10.1    | 1             |


    Scenario Outline: Changing memberlist in valid relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations has 1 row

        Given the OSM data
            """
            r30 v2 dV Ttype=multipolygon <new memberlist>
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations has <expected rows> rows

        Examples:
            | new memberlist | expected rows |
            | Mw20@          | 0             |
            | Mw21@,w20@     | 1             |


    Scenario: Changing tags keeps relation
        Given the grid
            | 13 | 12 |
            | 10 | 11 |
        And the OSM data
            """
            w20 v1 dV Nn10,n11,n12
            w21 v1 dV Nn12,n13,n10
            r30 v1 dV Ttype=multipolygon,natural=wood Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_relations contains exactly
            | area_id | tags->'natural' | tags->'landuse' |
            | -30     | wood            | NULL            |

        Given the OSM data
            """
            r30 v2 dV Ttype=multipolygon,landuse=forest Mw20@,w21@
            """
        When running osm2pgsql flex with parameters
            | --slim | --append |
        Then table osm2pgsql_test_relations contains exactly
            | area_id | tags->'natural' | tags->'landuse' |
            | -30     | NULL            | forest          |

