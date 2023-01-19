Feature: Handling of tags

    Scenario: Write tags in different forms to table
        Given the OSM data
            """
            n10 v1 dV Tname=Paris x10.0 y10.0
            n11 v1 dV Tname=Nürnberg x10.0 y10.0
            n12 v1 dV Tname=Plzeň x10.0 y10.0
            n13 v1 dV Tname=Αθήνα x10.0 y10.0
            n14 v1 dV Tname=תל־אביב-יפו x10.0 y10.0
            n15 v1 dV Tname=عَمَّان x10.0 y10.0
            n16 v1 dV Tname=北京 x10.0 y10.0
            n17 v1 dV Tname=ရန်ကုန် x10.0 y10.0
            n18 v1 dV Tname=मुंबई x10.0 y10.0
            """
        And the lua style
            """
            local pois = osm2pgsql.define_node_table('osm2pgsql_test_pois', {
                { column = 'name', type = 'text' },
                { column = 'htags', type = 'hstore' },
                { column = 'jtags', type = 'json' },
                { column = 'btags', type = 'jsonb' },
            })

            function osm2pgsql.process_node(object)
                pois:insert{
                    name = object.tags.name,
                    htags = object.tags,
                    jtags = object.tags,
                    btags = object.tags
                }
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_pois contains exactly
            | node_id | name        | htags->'name' | jtags->>'name' | btags->>'name' |
            | 10      | Paris       | Paris         | Paris          | Paris          |
            | 11      | Nürnberg    | Nürnberg      | Nürnberg       | Nürnberg       |
            | 12      | Plzeň       | Plzeň         | Plzeň          | Plzeň          |
            | 13      | Αθήνα       | Αθήνα         | Αθήνα          | Αθήνα          |
            | 14      | תל־אביב-יפו | תל־אביב-יפו   | תל־אביב-יפו    | תל־אביב-יפו    |
            | 15      | عَمَّان        | عَمَّان          | عَمَّان           | عَمَّان           |
            | 16      | 北京        | 北京          | 北京           | 北京           |
            | 17      | ရန်ကုန်        | ရန်ကုန်          | ရန်ကုန်           | ရန်ကုန်           |
            | 18      | मुंबई         | मुंबई           | मुंबई            | मुंबई            |

    Scenario: Write tags with special characters in different forms to table
        Given the OSM data
            """
            n10 v1 dV Tname= x10.0 y10.0
            n11 v1 dV Tname=<%20%> x10.0 y10.0
            n12 v1 dV Tname=<%09%> x10.0 y10.0
            n13 v1 dV Tname=<%1B%%0A%> x10.0 y10.0
            n14 v1 dV Tname=<%01%%1F%> x10.0 y10.0
            """
        And the lua style
            """
            local pois = osm2pgsql.define_node_table('osm2pgsql_test_pois', {
                { column = 'name', type = 'text' },
                { column = 'htags', type = 'hstore' },
                { column = 'jtags', type = 'json' },
                { column = 'btags', type = 'jsonb' },
            })

            function osm2pgsql.process_node(object)
                pois:insert{
                    name = object.tags.name,
                    htags = object.tags,
                    jtags = object.tags,
                    btags = object.tags
                }
            end
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_pois contains exactly
            | node_id | encode(name::bytea, 'hex') | encode((htags->'name')::bytea, 'hex') | encode((jtags->>'name')::bytea, 'hex') | encode((btags->>'name')::bytea, 'hex') |
            | 10      |                            |                                       |                                        |                                        |
            | 11      | 3c203e                     | 3c203e                                | 3c203e                                 | 3c203e                                 |
            | 12      | 3c093e                     | 3c093e                                | 3c093e                                 | 3c093e                                 |
            | 13      | 3c1b0a3e                   | 3c1b0a3e                              | 3c1b0a3e                               | 3c1b0a3e                               |
            | 14      | 3c011f3e                   | 3c011f3e                              | 3c011f3e                               | 3c011f3e                               |

