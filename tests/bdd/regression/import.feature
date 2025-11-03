Feature: Imports of the test database

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

    Scenario: Import non-slim
        When running osm2pgsql pgsql

        Then table planet_osm_point has 1342 rows
        And table planet_osm_polygon contains
            | count(*) | sum(ST_Area(way))::int | sum(way_area)::int |
            | 4130     | 1247245186             | 1247243136         |
        And table planet_osm_line contains
            | count(*) | sum(ST_Length(way))::int |
            | 3231     | 4211350                  |
        And table planet_osm_roads contains
            | count(*) | sum(ST_Length(way))::int |
            | 375      | 2032023                  |

        And there are no tables planet_osm_nodes, planet_osm_ways, planet_osm_rels


    Scenario: Import non-slim in WGS84
        When running osm2pgsql pgsql with parameters
            | -l |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_line has 3229 rows
        And table planet_osm_roads has 374 rows
        And table planet_osm_polygon has 4130 rows

        And there are no tables planet_osm_nodes, planet_osm_ways, planet_osm_rels


    Scenario Outline: Import slim
        When running osm2pgsql pgsql with parameters
            | --slim |
            | <drop> |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_polygon contains
            | count(*) | sum(ST_Area(way))::int | sum(way_area)::int |
            | 4130     | 1247245186             | 1247243136         |
        And table planet_osm_line contains
            | count(*) | sum(ST_Length(way))::int |
            | 3231     | 4211350                  |
        And table planet_osm_roads contains
            | count(*) | sum(ST_Length(way))::int |
            | 375      | 2032023                  |

        And there are <have table> planet_osm_nodes, planet_osm_ways, planet_osm_rels

        Examples:
            | drop   | have table |
            |        | tables     |
            | --drop | no tables  |


    Scenario Outline: Import with Lua tagtransform
        When running osm2pgsql pgsql
            | --create                   |
            | <slim>                     |
            | --tag-transform-script     |
            | {STYLE_DATA_DIR}/style.lua |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_polygon contains
            | count(*) | sum(ST_Area(way))::int | sum(way_area)::int |
            | 4136     | 1272140688             | 1272138496         |
        And table planet_osm_line contains
            | count(*) | sum(ST_Length(way))::int |
            | 3231     | 4211350                  |
        And table planet_osm_roads contains
            | count(*) | sum(ST_Length(way))::int |
            | 375      | 2032023                  |

        And there are <have table> planet_osm_nodes, planet_osm_ways, planet_osm_rels

        Examples:
            | slim   | have table |
            | --slim | tables     |
            |        | no tables  |

    Scenario Outline: Import slim with hstore
        When running osm2pgsql pgsql with parameters
            | --slim   |
            | <param1> |
            | <param2> |
            | <param3> |

        Then table planet_osm_point has 1360 rows
        And table planet_osm_line has 3254 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4131 rows

        And there are <has tables> planet_osm_nodes, planet_osm_ways, planet_osm_rels

        Examples:
            | param1    | param2             | param3 | has tables |
            | --hstore  | --hstore-add-index | --drop | no tables  |
            | -k        |                    |        | tables     |
            | --hstore  | --hstore-add-index |        | tables     |


    Scenario: Import slim with hstore all
        When running osm2pgsql pgsql with parameters
            | --slim   | -j |

        Then table planet_osm_point contains
            | count(*) | sum(array_length(akeys(tags), 1)) |
            | 1360     | 4228                              |
        Then table planet_osm_line contains
            | count(*) | sum(array_length(akeys(tags), 1)) |
            | 3254     | 10387                             |
        Then table planet_osm_roads contains
            | count(*) | sum(array_length(akeys(tags), 1)) |
            | 375      | 2317                              |
        Then table planet_osm_polygon contains
            | count(*) | sum(array_length(akeys(tags), 1)) |
            | 4131     | 9538                              |


    Scenario Outline: Import slim with various tweaks
        When running osm2pgsql pgsql with parameters
            | -s |
            | <param1> |
            | <param2> |
            | <param3> |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_polygon contains
            | count(*) | sum(ST_Area(way))::int | sum(way_area)::int |
            | 4130     | 1247245186             | 1247243136         |
        And table planet_osm_line contains
            | count(*) | sum(ST_Length(way))::int |
            | 3231     | 4211350                  |
        And table planet_osm_roads contains
            | count(*) | sum(ST_Length(way))::int |
            | 375      | 2032023                  |

        And there are tables planet_osm_nodes, planet_osm_ways, planet_osm_rels

        Examples:
            | param1              | param2 | param3 |
            | --number-processes  | 16     |        |
            | --number-processes  | 8      | -C1    |
            | -C0                 |        |        |
            | -z                  | name:  |        |
            | --hstore-match-only | -k     |        |
            | --hstore-match-only | -x     | -k     |

        Examples: Tablespaces
            | param1                 | param2         | param3 |
            | --tablespace-main-data | tablespacetest |        |
            | --tablespace-main-index| tablespacetest |        |
            | --tablespace-slim-data | tablespacetest |        |
            | --tablespace-slim-index| tablespacetest |        |


    Scenario: Import slim with hstore and extra tags
        When running osm2pgsql pgsql with parameters
            | --slim |
            | -j |
            | -x |

        Then table planet_osm_point contains
            | count(*) | every(tags ?& ARRAY['osm_user', 'osm_version', 'osm_uid', 'osm_changeset']) |
            | 1360     | True |
        Then table planet_osm_line contains
            | count(*) | every(tags ?& ARRAY['osm_user', 'osm_version', 'osm_uid', 'osm_changeset']) |
            | 3254     | True |
        Then table planet_osm_roads contains
            | count(*) | every(tags ?& ARRAY['osm_user', 'osm_version', 'osm_uid', 'osm_changeset']) |
            | 375      | True |
        Then table planet_osm_polygon contains
            | count(*) | every(tags ?& ARRAY['osm_user', 'osm_version', 'osm_uid', 'osm_changeset']) |
            | 4131     | True |
