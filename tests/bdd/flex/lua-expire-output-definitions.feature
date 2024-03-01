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

    Scenario: Filename in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
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
                table = 'bar',
                schema = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The expire output field must contain a 'schema' string field (or nil for default: 'public').
            """

    Scenario: Table in expire output definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
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
                maxzoom = 123,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'maxzoom' field in a expire output must be between 1 and 20.
            """

    Scenario: Minzoom value in expire output definition has to be in range
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                maxzoom = 12,
                minzoom = -3,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'minzoom' field in a expire output must be between 1 and 'maxzoom'.
            """

    Scenario: Minzoom value in expire output definition has to be smaller than maxzoom
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_expire_output({
                maxzoom = 12,
                minzoom = 14,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'minzoom' field in a expire output must be between 1 and 'maxzoom'.
            """

