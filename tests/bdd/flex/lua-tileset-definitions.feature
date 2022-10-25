Feature: Tileset definitions in Lua file

    Scenario: Tileset definition needs a Lua table parameter
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset()
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Argument #1 to 'define_tileset' must be a Lua table.
            """

    Scenario: Tileset definition needs a name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({})
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The tileset must contain a 'name' string field.
            """

    Scenario: Name in tileset definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = false,
                filename = 'foo'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The tileset must contain a 'name' string field.
            """

    Scenario: Filename in tileset definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                filename = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The tileset field must contain a 'filename' string field (or nil for default: '').
            """

    Scenario: Schema in tileset definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                table = 'bar',
                schema = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The tileset field must contain a 'schema' string field (or nil for default: '').
            """

    Scenario: Table in tileset definition has to be a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                table = false
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The tileset field must contain a 'table' string field (or nil for default: '').
            """

    Scenario: Maxzoom value in tileset definition has to be an integer
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                maxzoom = 'bar',
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'maxzoom' field in a tileset must contain an integer.
            """

    Scenario: Minzoom value in tileset definition has to be an integer
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                maxzoom = 12,
                minzoom = 'bar',
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'minzoom' field in a tileset must contain an integer.
            """

    Scenario: Maxzoom value in tileset definition has to be in range
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
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

    Scenario: Minzoom value in tileset definition has to be in range
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                maxzoom = 12,
                minzoom = -3,
                filename = 'somewhere'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Value of 'minzoom' field must be between 1 and 20.
            """

    Scenario: Can not create two tilesets with the same name
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            osm2pgsql.define_tileset({
                name = 'foo',
                maxzoom = 12,
                filename = 'somewhere'
            })
            osm2pgsql.define_tileset({
                name = 'foo',
                maxzoom = 13,
                filename = 'somewhereelse'
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Tileset with name 'foo' already exists.
            """

