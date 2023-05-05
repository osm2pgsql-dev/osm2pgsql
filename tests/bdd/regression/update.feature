Feature: Updates to the test database

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'

    Scenario Outline: Simple updates with various parameters
        When running osm2pgsql pgsql with parameters
            | --slim   |
            | <param1> |
            | <param2> |
            | <param3> |

        Given the input file '000466354.osc.gz'
        When running osm2pgsql pgsql with parameters
            | -a       |
            | --slim   |
            | <param1> |
            | <param2> |
            | <param3> |

        Then table planet_osm_point has 1457 rows
        And table planet_osm_line has 3274 rows
        And table planet_osm_roads has 380 rows
        And table planet_osm_polygon has 4277 rows
        And table osm2pgsql_settings has 5 rows

        Examples:
            | param1             | param2 | param3 |
            |                    |        |        |
            | --number-processes | 15     |        |
            | --number-processes | 8      | -C1    |
            | -C0                |        |        |
            | -z                 | name:  |        |
            | --hstore-match-only| -k     |        |
            | --hstore-match-only| -k     | -x     |

       Examples: with tablespaces
            | param1                 | param2         | param3 |
            | --tablespace-main-data | tablespacetest |        |
            | --tablespace-main-index| tablespacetest |        |
            | --tablespace-slim-data | tablespacetest |        |
            | --tablespace-slim-index| tablespacetest |        |


    Scenario Outline: Simple updates with hstore
        When running osm2pgsql pgsql with parameters
            | --slim   |
            | <param1> |
            | <param2> |

        Given the input file '000466354.osc.gz'
        When running osm2pgsql pgsql with parameters
            | -a       |
            | --slim   |
            | <param1> |
            | <param2> |

        Then table planet_osm_point has 1475 rows
        And table planet_osm_line has 3297 rows
        And table planet_osm_roads has 380 rows
        And table planet_osm_polygon has 4278 rows

        Examples:
            | param1             | param2   |
            | -k                 |          |
            | -j                 |          |
            | -j                 | -x       |
            | --hstore-add-index | --hstore |


    Scenario: Simple updates with lua tagtransform
        Given the default lua tagtransform
        When running osm2pgsql pgsql with parameters
            | --slim   |

        Given the input file '000466354.osc.gz'
        When running osm2pgsql pgsql with parameters
            | -a       |
            | --slim   |

        Then table planet_osm_point has 1457 rows
        And table planet_osm_line has 3274 rows
        And table planet_osm_roads has 380 rows
        And table planet_osm_polygon has 4283 rows

