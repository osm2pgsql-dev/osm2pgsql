Feature: Handling of timestamps

    Scenario: Write tags in different forms to table
        Given the OSM data
            """
            n10 v1 dV t2020-12-12T11:22:33Z Tts=20260102T123456Z x10.0 y10.0
            n11 v1 dV t2020-12-12T11:22:33Z Tts=2026-02-03T01:23:45Z x10.0 y10.0
            """
        And the lua style
            """
            local t = osm2pgsql.define_node_table('osm2pgsql_test', {
                { column = 'ts', type = 'timestamp' },
                { column = 'ts_tz', type = 'timestamptz' },
            })

            function osm2pgsql.process_node(object)
                t:insert{
                    ts = object.tags.ts,
                    ts_tz = object.tags.ts,
                }
                t:insert{
                    ts = object.timestamp,
                    ts_tz = object.timestamp,
                }
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test contains exactly
            | node_id | ts::text            | to_char(ts_tz AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') |
            | 10      | 2026-01-02 12:34:56 | 2026-01-02T12:34:56Z                                            |
            | 11      | 2026-02-03 01:23:45 | 2026-02-03T01:23:45Z                                            |
            | 10      | 2020-12-12 11:22:33 | 2020-12-12T11:22:33Z                                            |
            | 11      | 2020-12-12 11:22:33 | 2020-12-12T11:22:33Z                                            |

