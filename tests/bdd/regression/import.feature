Feature: Imports of the test database

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

    Scenario: Import non-slim
        When running osm2pgsql pgsql

        Then table planet_osm_point has 1342 rows
        And table planet_osm_line has 3231 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4130 rows

        Then the sum of 'cast(ST_Area(way) as numeric)' in table planet_osm_polygon is 1247245186
        And the sum of 'cast(way_area as numeric)' in table planet_osm_polygon is 1247245413
        And the sum of 'ST_Length(way)' in table planet_osm_line is 4211350
        And the sum of 'ST_Length(way)' in table planet_osm_roads is 2032023

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
        And table planet_osm_line has 3231 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4130 rows

        Then the sum of 'cast(ST_Area(way) as numeric)' in table planet_osm_polygon is 1247245186
        And the sum of 'cast(way_area as numeric)' in table planet_osm_polygon is 1247245413
        And the sum of 'ST_Length(way)' in table planet_osm_line is 4211350
        And the sum of 'ST_Length(way)' in table planet_osm_roads is 2032023

        And there are <have table> planet_osm_nodes, planet_osm_ways, planet_osm_rels

        Examples:
            | drop   | have table |
            |        | tables     |
            | --drop | no tables  |


    Scenario Outline: Import with Lua tagtransform
        Given the default lua tagtransform
        When running osm2pgsql pgsql
            | --create |
            | <slim> |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_line has 3231 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4136 rows

        Then the sum of 'cast(ST_Area(way) as numeric)' in table planet_osm_polygon is 1272140688
        And the sum of 'cast(way_area as numeric)' in table planet_osm_polygon is 1272140891
        And the sum of 'ST_Length(way)' in table planet_osm_line is 4211350
        And the sum of 'ST_Length(way)' in table planet_osm_roads is 2032023

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

        Then table planet_osm_point has 1360 rows
        And table planet_osm_line has 3254 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4131 rows

        Then the sum of 'array_length(akeys(tags),1)' in table planet_osm_point is 4228
        And the sum of 'array_length(akeys(tags),1)' in table planet_osm_roads is 2317
        And the sum of 'array_length(akeys(tags),1)' in table planet_osm_line is 10387
        And the sum of 'array_length(akeys(tags),1)' in table planet_osm_polygon is 9538


    Scenario Outline: Import slim with various tweaks
        When running osm2pgsql pgsql with parameters
            | -s |
            | <param1> |
            | <param2> |
            | <param3> |

        Then table planet_osm_point has 1342 rows
        And table planet_osm_line has 3231 rows
        And table planet_osm_roads has 375 rows
        And table planet_osm_polygon has 4130 rows

        Then the sum of 'cast(ST_Area(way) as numeric)' in table planet_osm_polygon is 1247245186
        And the sum of 'cast(way_area as numeric)' in table planet_osm_polygon is 1247245413
        And the sum of 'ST_Length(way)' in table planet_osm_line is 4211350
        And the sum of 'ST_Length(way)' in table planet_osm_roads is 2032023

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
            | --slim   | -j | -x |

        Then table planet_osm_point has 1360 rows with condition
            """
            tags ? 'osm_user' AND
            tags ? 'osm_version' AND
            tags ? 'osm_uid' AND
            tags ? 'osm_changeset'
            """
        And table planet_osm_line has 3254 rows with condition
            """
            tags ? 'osm_user' AND
            tags ? 'osm_version' AND
            tags ? 'osm_uid' AND
            tags ? 'osm_changeset'
            """
        And table planet_osm_roads has 375 rows with condition
            """
            tags ? 'osm_user' AND
            tags ? 'osm_version' AND
            tags ? 'osm_uid' AND
            tags ? 'osm_changeset'
            """
        And table planet_osm_polygon has 4131 rows with condition
            """
            tags ? 'osm_user' AND
            tags ? 'osm_version' AND
            tags ? 'osm_uid' AND
            tags ? 'osm_changeset'
            """

