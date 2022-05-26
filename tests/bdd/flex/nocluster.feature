Feature: Test flex config without clustering

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

        And the lua style
            """
            local dtable = osm2pgsql.define_node_table('osm2pgsql_test_point', {
                { column = 'tags', type = 'hstore' },
                { column = 'geom', type = 'point' },
            }, { cluster = 'no' })

            function is_empty(some_table)
                return next(some_table) == nil
            end

            function clean_tags(tags)
                tags.odbl = nil
                tags.created_by = nil
                tags.source = nil
                tags['source:ref'] = nil
                tags['source:name'] = nil
            end

            function osm2pgsql.process_node(data)
                clean_tags(data.tags)
                if is_empty(data.tags) then
                    return
                end

                dtable:add_row({
                    tags = data.tags
                })
            end
            """

    Scenario: Import non-slim without clustering
        When running osm2pgsql flex
        Then table osm2pgsql_test_point has 1362 rows

    Scenario: Import slim without clustering
        When running osm2pgsql flex with parameters
            | --slim |
        Then table osm2pgsql_test_point has 1362 rows
