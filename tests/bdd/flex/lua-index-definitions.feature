Feature: Index definitions in Lua file

    Scenario: Indexes field in table definition must be an array
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = true
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'indexes' field in definition of table 'mytable' is not an array.
            """

    Scenario: No indexes field in table definition gets you a default index
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING gist (geom)%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Empty indexes field in table definition gets you no index
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {}
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable'
            | schemaname | tablename |

    Scenario: Explicitly setting an index column works
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree' }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Explicitly setting multiple indexes
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree' },
                    { column = 'geom', method = 'gist' },
                    { column = { 'name', 'tags' }, method = 'btree' }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%'
            | schemaname | tablename |
            | public     | mytable   |
        And SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING gist (geom)%'
            | schemaname | tablename |
            | public     | mytable   |
        And SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name, tags)%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Method can not be missing
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Index definition must contain a 'method' string field.
            """

    Scenario: Method must be valid
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'ERROR' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Unknown index method 'ERROR'.
            """

    Scenario: Column can not be missing
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { method = 'btree' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            You must set either the 'column' or the 'expression' field in index definition.
            """

    Scenario: Column must exist
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'foo', method = 'btree' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Unknown column 'foo' in table 'mytable'.
            """

    Scenario: Column and expression together doesn't work
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', expression = 'lower(name)', method = 'btree' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            You must set either the 'column' or the 'expression' field in index definition.
            """

    Scenario: Expression indexes work
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { expression = 'lower(name)', method = 'btree' },
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (lower(name))%'
            | schemaname | tablename |
            | public     | mytable   |

    @needs-pg-index-includes
    Scenario: Include field must be a string or array
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', include = true }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            The 'include' field in an index definition must contain a string or an array.
            """

    @needs-pg-index-includes
    Scenario: Include field must contain a valid column
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', include = 'foo' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Unknown column 'foo' in table 'mytable'.
            """

    @needs-pg-index-includes
    Scenario: Include field works with string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', include = 'tags' }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%' AND indexdef LIKE '%INCLUDE (tags)%'
            | schemaname | tablename |
            | public     | mytable   |

    @needs-pg-index-includes
    Scenario: Include field works with array
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', include = { 'tags' } }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%' AND indexdef LIKE '%INCLUDE (tags)%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Tablespace needs a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', tablespace = true }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Index definition field 'tablespace' must be a string field.
            """

    Scenario: Empty tablespace is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', tablespace = '' }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Unique needs a boolean
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', unique = 'foo' }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Index definition field 'unique' must be a boolean field.
            """

    Scenario: Unique index works
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', unique = true }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%' AND indexdef LIKE '%UNIQUE%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Where condition needs a string
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', where = true }
                }
            })
            """
        Then running osm2pgsql flex fails
        And the error output contains
            """
            Index definition field 'where' must be a string field.
            """

    Scenario: Where condition works
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                },
                indexes = {
                    { column = 'name', method = 'btree', where = 'name = lower(name)' }
                }
            })
            """
        When running osm2pgsql flex
        Then SELECT schemaname, tablename FROM pg_catalog.pg_indexes WHERE tablename = 'mytable' AND indexdef LIKE '%USING btree (name)%' AND indexdef LIKE '%WHERE (name = lower(name))%'
            | schemaname | tablename |
            | public     | mytable   |

    Scenario: Don't create id index if the configuration doesn't mention it
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                }
            })
            """
        When running osm2pgsql flex
        Then table pg_catalog.pg_indexes has 0 rows with condition
            """
            schemaname = 'public' AND tablename = 'mytable' AND indexname LIKE '%node_id%'
            """

    Scenario: Don't create id index if the configuration doesn't says so
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id', create_index = 'auto' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                }
            })
            """
        When running osm2pgsql flex
        Then table pg_catalog.pg_indexes has 0 rows with condition
            """
            schemaname = 'public' AND tablename = 'mytable' AND indexname LIKE '%node_id%'
            """

    Scenario: Always create id index if the configuration says so
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id', create_index = 'always' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'tags', type = 'jsonb' },
                    { column = 'geom', type = 'geometry' },
                }
            })
            """
        When running osm2pgsql flex
        Then table pg_catalog.pg_indexes has 1 rows with condition
            """
            schemaname = 'public' AND tablename = 'mytable' AND indexname LIKE '%node_id%'
            """

