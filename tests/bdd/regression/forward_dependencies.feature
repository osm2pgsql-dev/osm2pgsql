Feature: Test forward propagation of changes

    Background:
        Given the OSM data
            """
            n10 v1 x1.0 y1.0
            n11 v1 x1.0 y2.0
            n12 v1 x2.0 y2.0 Tnatural=tree
            n13 v1 x3.0 y3.0
            n14 v1 x3.1 y3.1
            n15 v1 x0.0 y0.0
            n16 v1 x0.0 y0.1
            n17 v1 x0.1 y0.1
            w20 v1 Nn10,n11,n12,n10 Tlanduse=forest
            w21 v1 Nn13,n14 Thighway=primary
            w22 v1 Nn15,n16
            w23 v1 Nn16,n17,n15
            r30 v1 Mw22@,w23@ Ttype=multipolygon,natural=water
            """

    Scenario: Node changes are forwarded to ways and relations by default
        When running osm2pgsql pgsql with parameters
            | --slim | --latlong |
        Given the OSM data
            """
            n13 v2 x3.1 y3.0
            w23 v2 Nn16,n17
            """
        When running osm2pgsql pgsql with parameters
            | --slim | -a | --latlong |

        Then table planet_osm_point has 1 row
        Then table planet_osm_line has 1 row
        Then table planet_osm_line has 0 rows with condition
            """
            abs(ST_X(ST_StartPoint(way)) - 3.0) < 0.0001
            """
        Then table planet_osm_line has 1 row with condition
            """
            abs(ST_X(ST_StartPoint(way)) - 3.1) < 0.0001
            """
        Then table planet_osm_roads has 1 row
        Then table planet_osm_polygon has 1 row


    Scenario: Node changes are not forwarded when forwarding is disabled
        When running osm2pgsql pgsql with parameters
            | --slim | --latlong |
        Given the OSM data
            """
            n13 v2 x3.1 y3.0
            w23 v2 Nn16,n17
            """
        When running osm2pgsql pgsql with parameters
            | --slim | -a | --latlong | --with-forward-dependencies=false |

        Then table planet_osm_point has 1 row
        Then table planet_osm_line has 1 row
        Then table planet_osm_line has 1 row with condition
            """
            abs(ST_X(ST_StartPoint(way)) - 3.0) < 0.0001
            """
        Then table planet_osm_line has 0 rows with condition
            """
            abs(ST_X(ST_StartPoint(way)) - 3.1) < 0.0001
            """
        Then table planet_osm_roads has 1 row
        Then table planet_osm_polygon has 2 rows

