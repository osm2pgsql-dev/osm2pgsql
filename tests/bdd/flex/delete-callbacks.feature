Feature: Test for delete callbacks

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local change = osm2pgsql.define_table{
                name = 'change',
                ids = {type = 'any', id_column = 'osm_id', type_column = 'osm_type'},
                columns = {
                    { column = 'extra', type = 'int' }
                }}

            local function process_delete(object)
                change:insert{extra = object.version}
            end

            osm2pgsql.process_deleted_node = process_delete
            osm2pgsql.process_deleted_way = process_delete
            osm2pgsql.process_deleted_relation = process_delete
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table change contains exactly
            | osm_type | osm_id |

        Given the SQL statement grouped_counts
            """
            SELECT osm_type,
                   count(*),
                   sum(extra) AS sum_extra,
                   sum(osm_id) AS sum_osm_id
            FROM change
            GROUP BY osm_type
            """

    Scenario: Delete callbacks are called
        Given the input file '008-ch.osc.gz'
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then statement grouped_counts returns exactly
            | osm_type | count | sum_extra |
            | N        | 16773 | 16779     |
            | W        | 4     | 9         |
            | R        | 1     | 3         |

        Given the input file '008-ch.osc.gz'
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then statement grouped_counts returns exactly
            | osm_type | count | sum_osm_id |
            | N        | 16773 | 37856781001834 |
            | W        | 4     | 350933407 |
            | R        | 1     | 2871571 |

    Scenario: No object payload is available
        Given the lua style
            """
            local change = osm2pgsql.define_table{
                name = 'change',
                ids = {type = 'any', id_column = 'osm_id', type_column = 'osm_type'},
                columns = {
                    { column = 'extra', type = 'int' }
                }}

            function osm2pgsql.process_deleted_node(object)
                change:insert{extra = object.tags == nil and 1 or 0}
            end

            function osm2pgsql.process_deleted_way(object)
                change:insert{extra = object.nodes == nil and 1 or 0}
            end

            function osm2pgsql.process_deleted_relation(object)
                change:insert{extra = object.members == nil and 1 or 0}
            end


            """
        Given the input file '008-ch.osc.gz'
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then statement grouped_counts returns exactly
            | osm_type | count | sum_extra |
            | N        | 16773 | 16773 |
            | W        | 4     | 4 |
            | R        | 1     | 1 |


    Scenario: Geometries are not valid for deleted objects
        Given the lua style
            """
            change = osm2pgsql.define_table{
                name = 'change',
                ids = {type = 'any', id_column = 'osm_id', type_column = 'osm_type'},
                columns = {
                    { column = 'extra', sql_type = 'int' }
                }}

            function osm2pgsql.process_deleted_node(object)
                change:insert{extra = object.as_point == nil and 1 or 0}
            end

            function osm2pgsql.process_deleted_way(object)
                change:insert{extra = object.as_linestring == nil and 1 or 0}
            end

            function osm2pgsql.process_deleted_relation(object)
                change:insert{extra = object.as_geometrycollection == nil and 1 or 0}
            end


            """
        Given the input file '008-ch.osc.gz'
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then statement grouped_counts returns exactly
            | osm_type | count | sum_extra |
            | N        | 16773 | 16773|
            | W        | 4     | 4|
            | R        | 1     | 1|
