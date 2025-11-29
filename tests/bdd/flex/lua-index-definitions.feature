Feature: Index definitions in Lua file

    Background:
        Given the SQL statement mytable_indexes
            """
            SELECT indexdef, indisprimary as is_primary
            FROM pg_catalog.pg_index, pg_catalog.pg_indexes
            WHERE schemaname = 'public'
                  AND tablename = 'mytable'
                  AND indrelid = tablename::regclass
                  AND indexrelid = indexname::regclass
            """

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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING gist (geom) |

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
        Then statement mytable_indexes returns exactly
            | indexdef |

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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (name) |

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
        Then statement mytable_indexes returns exactly
            | indexdef@substr |
            | USING btree (name) |
            | USING gist (geom) |
            | USING btree (name, tags) |

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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (lower(name)) |

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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (name) INCLUDE (tags) |

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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (name) INCLUDE (tags) |

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
            Index definition field must contain a 'tablespace' string field (or nil for default: '').
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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (name) |

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
        Then statement mytable_indexes returns
            | indexdef@fullmatch |
            | .*UNIQUE.*USING btree \(name\).* |

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
            Index definition field must contain a 'where' string field (or nil for default: '').
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
        Then statement mytable_indexes returns
            | indexdef@substr |
            | USING btree (name) WHERE (name = lower(name)) |


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
        Then statement mytable_indexes returns exactly
            | indexdef@substr  |
            | USING gist (geom) |

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
        Then statement mytable_indexes returns exactly
            | indexdef@substr  |
            | USING gist (geom) |

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
        Then statement mytable_indexes returns
            | indexdef@substr      |
            | USING btree (node_id) |

    Scenario: Create a unique id index when requested
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id', create_index = 'unique' },
                columns = {}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table mytable has 1562 rows
        Then statement mytable_indexes returns
            | indexdef@fullmatch                           | is_primary |
            | CREATE UNIQUE INDEX .* USING .*\(node_id\).* | False      |

    Scenario: Create a primary key id index when requested
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local t = osm2pgsql.define_table({
                name = 'mytable',
                ids = { type = 'node', id_column = 'node_id', create_index = 'primary_key' },
                columns = {}
            })

            function osm2pgsql.process_node(object)
                t:insert({})
            end
            """
        When running osm2pgsql flex
        Then table mytable has 1562 rows
        Then statement mytable_indexes returns
            | indexdef@fullmatch                         | is_primary |
            | CREATE UNIQUE INDEX .* USING .*\(node_id\) | True       |
