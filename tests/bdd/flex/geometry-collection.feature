Feature: Create geometry collections from relations

    Background:
        Given the lua style
            """
            local dtable = osm2pgsql.define_table{
                name = 'osm2pgsql_test_collection',
                ids = { type = 'relation', id_column = 'osm_id' },
                columns = {
                    { column = 'name', type = 'text' },
                    { column = 'geom', type = 'geometrycollection', projection = 4326 }
                }
            }

            function osm2pgsql.process_relation(object)
                dtable:insert({
                    name = object.tags.name,
                    geom = object.as_geometrycollection()
                })
            end
            """

    Scenario Outline: Create geometry collection from different relations
        Given the 1.0 grid
            | 13 | 12 | 17 |    | 16 |
            | 10 | 11 |    | 14 | 15 |
        And the OSM data
            """
            w20 Nn10,n11,n12,n13,n10
            w21 Nn14,n15,n16
            r30 Tname=single Mw20@
            r31 Tname=multi Mw20@,w21@
            r32 Tname=mixed Mn17@,w21@
            r33 Tname=node Mn17@
            """
        When running osm2pgsql flex with parameters
            | -c      |
            | <param> |

        Then table osm2pgsql_test_collection contains exactly
            | osm_id | name   | ST_GeometryType(geom) | ST_NumGeometries(geom) | ST_GeometryType(ST_GeometryN(geom, 1)) |
            | 30     | single | ST_GeometryCollection | 1                      | ST_LineString                          |
            | 31     | multi  | ST_GeometryCollection | 2                      | ST_LineString                          |
            | 32     | mixed  | ST_GeometryCollection | 2                      | ST_Point                               |
            | 33     | node   | ST_GeometryCollection | 1                      | ST_Point                               |

        And table osm2pgsql_test_collection contains exactly
            | osm_id | ST_AsText(ST_GeometryN(geom, 1)) | ST_AsText(ST_GeometryN(geom, 2)) |
            | 30     | 10, 11, 12, 13, 10               | NULL                             |
            | 31     | 10, 11, 12, 13, 10               | 14, 15, 16                       |
            | 32     | 17                               | 14, 15, 16                       |
            | 33     | 17                               | NULL                             |

        Examples:
            | param  |
            |        |
            | --slim |

    Scenario: NULL entry generated for broken geometries
        Given the grid
            | 10 |
        And the OSM data
            """
            w20 Nn10
            r30 Tname=foo Mn11@
            r31 Tname=bar Mw20@
            r32 Tname=baz Mw21@
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_collection contains exactly
            | osm_id | name   | geom |
            | 30     | foo    | NULL |
            | 31     | bar    | NULL |
            | 32     | baz    | NULL |

    Scenario: Null geometry generated for broken way lines
        Given the grid
            | 10 |
        And the OSM data
            """
            w20 Nn10
            w21 Nn10,n10
            r30 Tname=w20 Mw20@
            r31 Tname=w21 Mw21@
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_collection contains exactly
            | osm_id | name  | geom |
            | 30     | w20   | NULL |
            | 31     | w21   | NULL |

    Scenario: No geometry generated for broken way lines, others are there
        Given the grid
            | 10 | 11 |
            | 13 | 12 |
        And the OSM data
            """
            w20 Nn10,n11,n12,n13,n10
            w21 Nn10
            w22 Nn10,n11,n13
            r30 Tname=three Mw20@,w21@,w22@
            """
        When running osm2pgsql flex

        Then table osm2pgsql_test_collection contains exactly
            | osm_id | name  | ST_NumGeometries(geom) | ST_AsText(ST_GeometryN(geom, 1)) | ST_AsText(ST_GeometryN(geom, 2)) |
            | 30     | three | 2                      | 10, 11, 12, 13, 10               | 10, 11, 13                       |

