Feature: Expire output definitions in Lua file

    Scenario: Expire output definition needs a Lua table parameter
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output()
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Argument #1 to 'define_expire_output' must be a Lua table.
            """

    Scenario: Expire output definition needs a name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({})
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output must contain a 'name' string field.
            """

    Scenario: Name in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = false,
                filename = 'foo'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output must contain a 'name' string field.
            """

    Scenario: Name in expire output definition can not be empty
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = '',
                filename = 'foo',
                maxzoom = 14
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output name can not be empty.
            """

    Scenario: Filename in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                filename = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output field must contain a 'filename' string field (or nil for default: '').
            """

    Scenario: Schema in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                table = 'bar',
                schema = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output field must contain a 'schema' string field (or nil for default: '').
            """

    Scenario: Table in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                table = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output field must contain a 'table' string field (or nil for default: '').
            """

    Scenario: Maxzoom value in expire output definition has to be an integer
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 'bar',
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'maxzoom' field in a expire output must contain an integer.
            """

    Scenario: Minzoom value in expire output definition has to be an integer
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 12,
                minzoom = 'bar',
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'minzoom' field in a expire output must contain an integer.
            """

    Scenario: Maxzoom value in expire output definition has to be in range
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 123,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Value of 'maxzoom' field must be between 1 and 20.
            """

    Scenario: Minzoom value in expire output definition has to be in range
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 12,
                minzoom = -3,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Value of 'minzoom' field must be between 1 and 'maxzoom'.
            """

    Scenario: Minzoom value in expire output definition has to be smaller than maxzoom
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 12,
                minzoom = 14,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Value of 'minzoom' field must be between 1 and 'maxzoom'.
            """

    Scenario: Can not define two expire outputs with the same name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 12,
                filename = 'somewhere'
            })
            osm2pgsql.define_expire_output({
                name = 'foo',
                maxzoom = 13,
                filename = 'somewhereelse'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Expire output with name 'foo' already exists.
            """

