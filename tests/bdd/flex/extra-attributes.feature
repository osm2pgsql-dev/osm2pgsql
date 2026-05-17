Feature: Tests for including extra attributes

    Background:
        Given the lua style
            """
            local attr_table = osm2pgsql.define_table{
                name = 'osm2pgsql_test_attr',
                ids = { type = 'way', id_column = 'way_id' },
                columns = {
                    { column = 'type', type = 'text' },
                    { column = 'tags', type = 'hstore' },
                    { column = 'version', type = 'int4' },
                    { column = 'changeset', type = 'int4' },
                    { column = 'timestamp', type = 'int4' },
                    { column = 'timestamp_ts', type = 'timestamp' },
                    { column = 'timestamp_ts_tz', type = 'timestamptz' },
                    { column = 'uid', type = 'int4' },
                    { column = 'user', type = 'text' },
                    { column = 'geom', type = 'linestring', not_null = true },
                }
            }

            function osm2pgsql.process_way(object)
                object.geom = object:as_linestring()
                a = object
                a.timestamp_ts = a.timestamp
                a.timestamp_ts_tz = a.timestamp
                attr_table:insert(a)
            end
            """

    Scenario: Importing data without extra attributes
        Given the grid
            | 11 | 12 |
            | 10 |    |
        And the OSM data
            """
            w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest Thighway=primary Nn10,n11,n12
            """
        When running osm2pgsql flex with parameters
            | --slim |

        Then table osm2pgsql_test_attr contains
            | type | way_id | tags->'highway' | tags->'osm_version' | version | changeset | timestamp  | timestamp_ts::text  | to_char(timestamp_ts_tz AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') | uid  | "user" |
            | way  | 20     | primary         | NULL                | 1       | 31        | 1578832496 | 2020-01-12 12:34:56 | 2020-01-12T12:34:56Z                                                      | 17   | test   |

        Given the grid
            |    |    |
            |    | 10 |
        When running osm2pgsql flex with parameters
            | --slim | --append |

        Then table osm2pgsql_test_attr contains
            | type |  way_id | tags->'highway' | tags->'osm_version' | version | changeset | timestamp | timestamp_ts | timestamp_ts_tz | uid  | "user" |
            | way  |  20     | primary         | NULL                | NULL    | NULL      | NULL      | NULL         | NULL            | NULL | NULL   |


    Scenario: Importing data with extra attributes
        Given the grid
            | 11 | 12 |
            | 10 |    |
        And the OSM data
            """
            w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest Thighway=primary Nn10,n11,n12
            """
        When running osm2pgsql flex with parameters
            | --slim | -x |

        Then table osm2pgsql_test_attr contains
            | type |  way_id | tags->'highway' | tags->'osm_version' | version | changeset | timestamp  | uid  | "user" |
            | way  |  20     | primary         | NULL                | 1       | 31        | 1578832496 | 17   | test   |

        Given the grid
            |    |    |
            |    | 10 |
        When running osm2pgsql flex with parameters
            | --slim | --append | -x |

        Then table osm2pgsql_test_attr contains
            | type |  way_id | tags->'highway' | tags->'osm_version' | version | changeset | timestamp  | uid | "user" |
            | way  |  20     | primary         | NULL                | 1       | 31        | 1578832496 | 17  | test   |

