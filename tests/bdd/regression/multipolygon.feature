Feature: Import and update of multipolygon areas

    Background:
        Given the input file 'test_multipolygon.osm'

        Given the SQL statement grouped_polygons
            """
            SELECT osm_id,
                   count(*) AS count,
                   sum(ST_Area(way))::int AS area,
                   sum(way_area)::int AS way_area
            FROM planet_osm_polygon
            GROUP BY osm_id
            """

    Scenario Outline: Import and update slim
        When running osm2pgsql pgsql with parameters
            | --slim   |
            | <param1> |

        Then there are tables planet_osm_nodes

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 12895             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | ST_Area(way)::int |
            | -24    | water     | 18501             |
            | 102    | water     | 12994             |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | ST_Length(way)::int |
            | 6      | residential | Name_way6 | 228                 |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then statement grouped_polygons returns
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 68494 | 68494    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then statement grouped_polygons returns
            | osm_id | count |
            | -25    | 1     |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon doesn't contain
            | osm_id |
            | 109    |
            | 104    |

        Then table planet_osm_polygon doesn't contain
            | osm_id | "natural" |
            | -33    | water     |

        Given the SQL statement geometries_polygon
            """
            SELECT osm_id,
                   CASE WHEN '<param1>' = '-G' THEN min(ST_NumGeometries(way))
                   ELSE count(*) END AS count
            FROM planet_osm_polygon
            GROUP BY osm_id
            """
        Then statement geometries_polygon returns
           | osm_id | count |
           | -13    | 2     |
           | -7     | 2     |


        Given the input file 'test_multipolygon_diff.osc'
        When running osm2pgsql pgsql with parameters
            | --slim |
            | -a |
            | <param1> |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 13949             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | ST_Area(way)::int |
            | -24    | water     | 18501             |
            | 102    | water     | 12994             |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | ST_Length(way)::int |
            | 6      | residential | Name_way6 | 228                 |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then statement grouped_polygons returns
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 29155 | 29155    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then statement grouped_polygons returns
            | osm_id | count |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon doesn't contain
            | osm_id |
            | -25    |
            | 109    |
            | 104    |

        Then table planet_osm_polygon doesn't contain
            | osm_id | "natural" |
            | -33    | water     |

        Examples:
            | param1 |
            |        |
            | -G     |
            | -k     |


    Scenario Outline: Import and update slim
        When running osm2pgsql pgsql with parameters
            | --slim                     |
            | <param1>                   |
            | --tag-transform-script     |
            | {STYLE_DATA_DIR}/style.lua |

        Then there are tables planet_osm_nodes

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 12895             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | ST_Area(way)::int |
            | -24    | water     | 18501             |
            | 102    | water     | 12994             |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | ST_Length(way)::int |
            | 6      | residential | Name_way6 | 228                 |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then statement grouped_polygons returns
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 68494 | 68494    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then statement grouped_polygons returns
            | osm_id | count |
            | -25    | 1     |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon doesn't contain
            | osm_id |
            | 109    |
            | 104    |

        Then table planet_osm_polygon doesn't contain
            | osm_id | "natural" |
            | -33    | water     |

        Given the SQL statement geometries_polygon
            """
            SELECT osm_id,
                   CASE WHEN '<param1>' = '-G' THEN min(ST_NumGeometries(way))
                   ELSE count(*) END AS count
            FROM planet_osm_polygon
            GROUP BY osm_id
            """
        Then statement geometries_polygon returns
           | osm_id | count |
           | -13    | 2     |
           | -7     | 2     |


        Given the input file 'test_multipolygon_diff.osc'
        When running osm2pgsql pgsql with parameters
            | --slim                     |
            | -a                         |
            | <param1>                   |
            | --tag-transform-script     |
            | {STYLE_DATA_DIR}/style.lua |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 13949             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |

        Then table planet_osm_polygon contains
            | osm_id | "natural" | ST_Area(way)::int |
            | -24    | water     | 18501             |
            | 102    | water     | 12994             |

        Then table planet_osm_line contains
            | osm_id | highway     | name      | ST_Length(way)::int |
            | 6      | residential | Name_way6 | 228                 |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name       | ST_NumInteriorRing(way) |
            | -3     | residential | Name_rel11 | 2                       |

        Then statement grouped_polygons returns
            | osm_id | area  | way_area |
            | -13    | 17581 | 17581    |
            | -7     | 16169 | 16169    |
            | -29    | 29155 | 29155    |
            | -39    | 10377 | 10378    |
            | -40    | 12397 | 12397    |

        Then statement grouped_polygons returns
            | osm_id | count |
            | 113    | 1     |
            | 118    | 1     |
            | 114    | 1     |
            | 107    | 1     |
            | 102    | 1     |
            | 138    | 1     |
            | 140    | 1     |

        Then table planet_osm_polygon doesn't contain
            | osm_id |
            | -25    |
            | 109    |
            | 104    |

        Then table planet_osm_polygon doesn't contain
            | osm_id | "natural" |
            | -33    | water     |

        Examples:
            | param1 |
            |        |
            | -k     |


     Scenario: Import with flat-node option
        When running osm2pgsql pgsql with parameters
            | -F | flat.store | --drop |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 12895             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |


     Scenario: Import and update with flat-node option
        When running osm2pgsql pgsql with parameters
            | --slim | -F | flat.store |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 12895             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |


        Given the input file 'test_multipolygon_diff.osc'
        When running osm2pgsql pgsql with parameters
            | --slim | -a | -F | flat.store |

        Then table planet_osm_polygon contains
            | osm_id | landuse     | name      | ST_Area(way)::int |
            | -1     | residential | Name_rel  | 13949             |
            | 4      | farmland    | Name_way3 | 3144              |
            | -8     | residential | Name_rel2 | 12894             |
            | 5      | farmland    | Name_way4 | 3144              |
            | -14    | residential | Name_way5 | 12894             |
            | -11    | residential | Name_rel6 | 11529             |
            | -3     | residential | Name_rel11| 9286              |
            | 83     | farmland    | NULL      | 24859             |

