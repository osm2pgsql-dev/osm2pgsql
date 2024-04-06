Feature: Test flex config without clustering

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

        And the lua style
            """
            local dtable = osm2pgsql.define_node_table('osm2pgsql_test_point', {
                { column = 'tags', type = 'hstore' },
                { column = 'geom', type = 'point', not_null = true },
            }, { cluster = 'no' })

            function osm2pgsql.process_node(data)
                dtable:insert({
                    tags = data.tags,
                    geom = data:as_point()
                })
            end
            """

    Scenario: Import non-slim without clustering
        When running osm2pgsql flex
        Then table osm2pgsql_test_point has 1562 rows

    Scenario: Import slim without clustering
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_point has 1562 rows

