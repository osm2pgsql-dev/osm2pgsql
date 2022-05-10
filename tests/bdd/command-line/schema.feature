Feature: Importing data into different DB schemas

    Scenario: Using a different schema for pgsql output tables
        Given the OSM data
            """
            n3948 Thighway=bus_stop,name=Bus x22.45 y-20.1444
            """
        Given the database schema osm

        When running osm2pgsql pgsql with parameters
            | --output-pgsql-schema=osm |

        Then table osm.planet_osm_point has 1 row
