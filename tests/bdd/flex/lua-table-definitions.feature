Feature: Table definitions in Lua file

    Scenario: Table definition needs a table parameter
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table()
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Argument #1 to 'define_table' must be a table.
            """

    Scenario: Table definition needs a name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({})
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The table must contain a 'name' string field.
            """

    Scenario: Name in table definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The table must contain a 'name' string field.
            """

    Scenario: Table definition needs a column list
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            No 'columns' field (or not an array) in table 'foo'.
            """

    Scenario: The columns field must contain a table
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                columns = 123
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            No 'columns' field (or not an array) in table 'foo'.
            """

    Scenario: Table with empty columns list is not okay if there are no ids
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                columns = {}
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            No columns defined for table 'foo'.
            """

    Scenario: Table with empty columns list is okay if there is an id column
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'foo',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table foo has 1562 rows

    Scenario: Can not create two tables with the same name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t1 = osm2pgsql.define_node_table('foo', {
                { column = 'bar' }
            })
            local t2 = osm2pgsql.define_node_table('foo', {
                { column = 'baz' }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Table with name 'foo' already exists.
            """
