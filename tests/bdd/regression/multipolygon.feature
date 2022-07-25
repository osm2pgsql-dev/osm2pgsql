Feature: Import and update of multipolygon areas

    Background:
        Given the input file 'test_multipolygon.osm'

    Scenario Outline: Import and update slim
        Given <lua> lua tagtransform
        When running osm2pgsql pgsql with parameters
            | --slim   |
            | <param1> |

        Then there are tables planet_osm_nodes

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | round(ST_Area(way)) |
            | -1     | residential | Name_rel  | 12895               |
            | 4      | farmland    | Name_way3 | 3144                |
            | -8     | residential | Name_rel2 | 12894               |
            | 5      | farmland    | Name_way4 | 3144                |
            | -14    | residential | Name_way5 | 12894               |
            | -11    | residential | Name_rel6 | 11529               |
            | -3     | residential | Name_rel11| 9286                |
            | 83     | farmland    | NULL      | 24859               |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | round(ST_Area(way)) |
            | -24    | water     | 18501               |
            | 102    | water     | 12994               |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | round(ST_Length(way)) |
            | 6      | residential | Name_way6 | 228                   |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then SELECT osm_id, round(sum(ST_Area(way))), round(sum(way_area::numeric)) FROM planet_osm_polygon GROUP BY osm_id
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 68494 | 68494    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then SELECT osm_id, count(*) FROM planet_osm_polygon GROUP BY osm_id
            | osm_id | count |
            | -25    | 1     |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon has 0 rows with condition
            """
            osm_id IN (109, 104)
            """

        Then table planet_osm_polygon has 0 rows with condition
            """
            osm_id = -33 and "natural" = 'water'
            """

        Then SELECT osm_id, CASE WHEN '<param1>' = '-G' THEN min(ST_NumGeometries(way)) ELSE count(*) END FROM planet_osm_polygon GROUP BY osm_id
           | osm_id | count |
           | -13    | 2     |
           | -7     | 2     |


        Given the input file 'test_multipolygon_diff.osc'
        When running osm2pgsql pgsql with parameters
            | --slim |
            | -a |
            | <param1> |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | round(ST_Area(way)) |
            | -1     | residential | Name_rel  | 13949               |
            | 4      | farmland    | Name_way3 | 3144                |
            | -8     | residential | Name_rel2 | 12894               |
            | 5      | farmland    | Name_way4 | 3144                |
            | -14    | residential | Name_way5 | 12894               |
            | -11    | residential | Name_rel6 | 11529               |
            | -3     | residential | Name_rel11| 9286                |
            | 83     | farmland    | NULL      | 24859               |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | round(ST_Area(way)) |
            | -24    | water     | 18501               |
            | 102    | water     | 12994               |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | round(ST_Length(way)) |
            | 6      | residential | Name_way6 | 228                   |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then SELECT osm_id, round(sum(ST_Area(way))), round(sum(way_area::numeric)) FROM planet_osm_polygon GROUP BY osm_id
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 29155 | 29155    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then SELECT osm_id, count(*) FROM planet_osm_polygon GROUP BY osm_id
            | osm_id | count |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon has 0 rows with condition
            """
            osm_id IN (-25, 109, 104)
            """

        Then table planet_osm_polygon has 0 rows with condition
            """
            osm_id = -33 and "natural" = 'water'
            """

        Examples:
            | param1 | lua         |
            |        | no          |
            | -G     | no          |
            | -k     | no          |
            |        | the default |
            | -k     | the default |


     Scenario: Import and update with flat-node option
        When running osm2pgsql pgsql with parameters
            | --slim | -F | flat.store |

        Then there is no table planet_osm_nodes

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | round(ST_Area(way)) |
            | -1     | residential | Name_rel  | 12895               |
            | 4      | farmland    | Name_way3 | 3144                |
            | -8     | residential | Name_rel2 | 12894               |
            | 5      | farmland    | Name_way4 | 3144                |
            | -14    | residential | Name_way5 | 12894               |
            | -11    | residential | Name_rel6 | 11529               |
            | -3     | residential | Name_rel11| 9286                |
            | 83     | farmland    | NULL      | 24859               |


        Given the input file 'test_multipolygon_diff.osc'
        When running osm2pgsql pgsql with parameters
            | --slim | -a | -F | flat.store |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | round(ST_Area(way)) |
            | -1     | residential | Name_rel  | 13949               |
            | 4      | farmland    | Name_way3 | 3144                |
            | -8     | residential | Name_rel2 | 12894               |
            | 5      | farmland    | Name_way4 | 3144                |
            | -14    | residential | Name_way5 | 12894               |
            | -11    | residential | Name_rel6 | 11529               |
            | -3     | residential | Name_rel11| 9286                |
            | 83     | farmland    | NULL      | 24859               |

