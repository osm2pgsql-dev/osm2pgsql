Feature: Tests for bad input data

    Scenario: Overly large relations are ignored
        Given the python-formatted OSM data
            """
            n1 x45 y34
            r1 Ttype=multipolygon M{','.join('n' + str(i) + '@' for i in range(33000))}
            """

        When running osm2pgsql pgsql with parameters
            | --slim |

        Then table planet_osm_nodes has 1 row
        Then table planet_osm_rels has 0 rows

