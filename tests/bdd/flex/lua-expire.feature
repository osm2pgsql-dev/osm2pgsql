Feature: Expire configuration in Lua file

    Scenario: Expire on a table must be on geometry column
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some', expire = {{ output = eo }} }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Expire only allowed for geometry columns in Web Mercator projection.
            """

    Scenario: Expire on a table must be on geometry column in 3857
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  projection = 4326,
                  expire = {{ output = eo }} }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Expire only allowed for geometry columns in Web Mercator projection.
            """

    Scenario: Expire reference must be a userdata ExpireOutput object
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some', type = 'geometry', expire = {{ output = 'abc' }} }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Expire output must be of type ExpireOutput.
            """

    Scenario: Expire on a table with 3857 geometry is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {{ output = eo }} }
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Directly specifying the expire output is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = eo }
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire with buffer option that's not a number fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 'notvalid' }
                }}
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Optional expire field 'buffer' must contain a number.
            """

    Scenario: Expire with invalid mode setting fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {{ output = eo, mode = 'foo' }} }
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Unknown expire mode 'foo'.
            """

    Scenario: Expire with full_area_limit that's not a number fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, full_area_limit = true }
                }}
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Optional expire field 'full_area_limit' must contain a number.
            """

    Scenario: Expire with boundary-only options is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 0.2, mode = 'boundary-only' }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire with hybrid options is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 0.2,
                      mode = 'hybrid', full_area_limit = 10000 }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire into table is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'tiles',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('nodes', {
                { column = 'geom',
                  type = 'point',
                  expire = {
                    { output = eo }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table nodes has 1562 rows
        And table tiles has 0 rows

