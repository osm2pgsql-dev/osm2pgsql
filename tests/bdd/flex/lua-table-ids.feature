Feature: Ids in table definitions in Lua file

    Scenario: Table definition without ids is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 1562 rows

    Scenario: Table definition with empty ids is not allowed
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = {},
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            The ids field must contain a 'type' string field.
            """

    Scenario: Table ids definition must contain a text id_column field
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 123 },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Unknown ids type: 123.
            """

    Scenario: Table ids definition must contain an id_column field
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node' },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            The ids field must contain a 'id_column' string field.
            """

    Scenario: Table ids definition must contain an id_column field
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', ids_column = false },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            The ids field must contain a 'id_column' string field.
            """

    Scenario: Table ids definition with type and id_column fields is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'abc' },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 1562 rows

    Scenario Outline:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = '<idtype>', id_column = 'abc' },
                columns = {{ column = 'bar', type = 'text' }}
            })
            """
        When running osm2pgsql flex
        Then execution is successful

        Examples:
            | idtype   |
            | node     |
            | way      |
            | relation |
            | area     |
            | any      |
            | tile     |

    Scenario: Table ids definition checks for special characters in column names
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'a"b"c' },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Special characters are not allowed in column names: 'a"b"c'.
            """

    Scenario: Table ids definition can contain cache field but needs right type
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'abc', cache = 'xxx' },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            The ids field 'cache' must be a boolean field.
            """

    Scenario: Table ids definition can contain boolean cache field (false)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'abc', cache = false },
                columns = {{ column = 'bar', type = 'text' }} })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 1562 rows

    Scenario: Table ids definition can contain boolean cache field (true)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'abc', cache = true },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 1562 rows

    Scenario: Table ids definition can contain false cache field for a way
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'way', id_column = 'abc', cache = false },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_way(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 7105 rows

    Scenario: Table ids definition can contain cache field only for nodes
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'way', id_column = 'abc', cache = true },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_way(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            ID cache only available for node ids.
            """

    Scenario: Error when accessing id cache of table that doesn't have one
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_way(object)
                t:in_id_cache({})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            No ID cache on table 'foo'.
            """

    Scenario: Error when accessing id cache of table that doesn't have one
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'node_id', cache = true },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_way(object)
                t:in_id_cache("error")
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Second parameter must be an array of ids.
            """

    Scenario: Calling in_id_cache() on something other than a table doesn't work
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'node_id', cache = true },
                columns = {{ column = 'bar', type = 'text' }}
            })

            function osm2pgsql.process_way(object)
                t.in_id_cache("error", {})
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            First parameter must be of type osm2pgsql.Table.
            """

