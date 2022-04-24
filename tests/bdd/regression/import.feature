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
