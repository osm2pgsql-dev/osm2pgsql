Feature: Test for correct id column generation

    Background:
        Given the 0.1 grid
           | 1 |   |  2 |
           | 3 |   |  4 |

    Scenario: Data can be inserted into tables without an iD column
        Given the lua style
            """
            local simple = osm2pgsql.define_table{
                name = 'simple',
                columns = {{ column = 'id', type = 'bigint'}}
            }

            function osm2pgsql.process_node(object)
                simple:insert{ id = object.id }
            end

            function osm2pgsql.process_way(object)
                simple:insert{ id = object.id }
            end

            function osm2pgsql.process_relation(object)
                simple:insert{ id = object.id }
            end
            """
        And the OSM data
            """
            n1 Tp=1
            n2 Tp=2
            w10 Tp=10 Nn1,n2,n4
            r100 Tp=100 Mn1@,n2@
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table simple contains
            | id  |
            | 1   |
            | 2   |
            | 10  |
            | 100 |
        Given the OSM data
            """
            n1 v2 dD
            w11 Tp=11 Nn1,n3
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table simple contains
            | id  |
            | 1   |
            | 2   |
            | 10  |
            | 11  |
            | 100 |

