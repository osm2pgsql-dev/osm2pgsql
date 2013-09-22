import unittest
import psycopg2
import os
from pwd import getpwnam
import subprocess

full_import_file="tests/liechtenstein-2013-08-03.osm.pbf"
multipoly_import_file="tests/test_multipolygon.osm" #This file contains a number of different multi-polygon test cases
diff_import_file="tests/000466354.osc.gz"
diff_multipoly_import_file="tests/test_multipolygon_diff.osc" #This file contains a number of different multi-polygon diff processing test cases

created_tablespace = 0

#****************************************************************
#****************************************************************
sql_test_statements=[
    ( 0, 'Basic point count', 'SELECT count(*) FROM planet_osm_point;', 1342 ),
    ( 1, 'Basic line count', 'SELECT count(*) FROM planet_osm_line;', 3300 ),
    ( 2, 'Basic road count', 'SELECT count(*) FROM planet_osm_roads;', 375 ),
    ( 3, 'Basic polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4128 ),
    ( 4,  'Basic latlon line count', 'SELECT count(*) FROM planet_osm_line;', 3298 ),
    ( 5, 'Basic latlon road count', 'SELECT count(*) FROM planet_osm_roads;', 374 ),
    ( 6, 'Basic post-diff point count', 'SELECT count(*) FROM planet_osm_point;', 1457 ),
    ( 7, 'Basic post-diff line count', 'SELECT count(*) FROM planet_osm_line;', 3344 ),
    ( 8, 'Basic post-diff road count', 'SELECT count(*) FROM planet_osm_roads;', 381 ),
    ( 9, 'Basic post-diff polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4275 ),
    ( 10, 'Absence of nodes table', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_nodes\'', 0),
    ( 11, 'Absence of way table', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_ways\'', 0),
    ( 12, 'Absence of rel line', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_rels\'', 0),
    ( 13, 'Basic polygon area', 'SELECT round(sum(cast(ST_Area(way) as numeric)),0) FROM planet_osm_polygon;', 1223800814),
    ( 14, 'Gazetteer place count', 'SELECT count(*) FROM place', 4500),
    ( 15, 'Gazetteer place node count', 'SELECT count(*) FROM place WHERE osm_type = \'N\'', 779),
    ( 16, 'Gazetteer place way count', 'SELECT count(*) FROM place WHERE osm_type = \'W\'', 3699),
    ( 17, 'Gazetteer place rel count', 'SELECT count(*) FROM place WHERE osm_type = \'R\'', 22),
    ( 18, 'Gazetteer post-diff place count', 'SELECT count(*) FROM place', 4554),
    ( 19, 'Gazetteer post-diff place node count', 'SELECT count(*) FROM place WHERE osm_type = \'N\'', 788),
    ( 20, 'Gazetteer post-diff place way count', 'SELECT count(*) FROM place WHERE osm_type = \'W\'', 3744),
    ( 21, 'Gazetteer post-diff place rel count', 'SELECT count(*) FROM place WHERE osm_type = \'R\'', 22),
    ( 22, 'Gazetteer housenumber count', 'SELECT count(*) FROM place WHERE housenumber is not null', 199),
    ( 23, 'Gazetteer post-diff housenumber count count', 'SELECT count(*) FROM place WHERE housenumber is not null', 199),
    ( 24, 'Gazetteer isin count', 'SELECT count(*) FROM place WHERE isin is not null', 239),
    ( 25, 'Gazetteer post-diff isin count count', 'SELECT count(*) FROM place WHERE isin is not null', 239),
    ( 26, 'Multipolygon basic case (Tags from outer way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -15 and landuse = \'residential\' and name = \'Name_way\'', 12894),
    ( 27, 'Multipolygon basic case (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -1 and landuse = \'residential\' and name = \'Name_rel\'', 12895),
    ( 28, 'Multipolygon named inner - outer (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -16 and landuse = \'residential\' and name = \'Name_way2\'', 12895),
    ( 29, 'Multipolygon named inner - inner way',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 4 and landuse = \'farmland\' and name = \'Name_way3\'', 3144),
    ( 30, 'Multipolygon named inner - outer (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -8 and landuse = \'residential\' and name = \'Name_rel2\'', 12894),
    ( 31, 'Multipolygon named inner - inner way',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 5 and landuse = \'farmland\' and name = \'Name_way4\'', 3144),
    ( 32, 'Multipolygon named same inner - outer (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -17 and landuse = \'residential\' and name = \'Name_way16\'', 12895),
    ( 33, 'Multipolygon named same inner - inner way absent',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 15', 0),
    ( 34, 'Multipolygon non-area inner - outer (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -14 and landuse = \'residential\' and name = \'Name_way5\'', 12893),
    ( 35, 'Multipolygon non-area inner - inner (Tags from way)',
      'SELECT round(ST_Length(way)) FROM planet_osm_line WHERE osm_id = 6 and highway = \'residential\' and name = \'Name_way6\'', 228),
    ( 36, 'Multipolygon 2 holes (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -18 and landuse = \'residential\' and name = \'Name_way7\'', 11823),
    ( 37, 'Multipolygon 2 holes (Tags from way)',
      'SELECT ST_NumInteriorRing(way) FROM planet_osm_polygon WHERE osm_id = -18 and landuse = \'residential\' and name = \'Name_way7\'', 2),
    ( 38, 'Multipolygon from multiple outer ways 0 holes (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -11 and landuse = \'residential\' and name = \'Name_rel6\'', 11528),
    ( 39, 'Multipolygon from multiple outer and multiple inner ways 2 holes (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -3 and landuse = \'residential\' and name = \'Name_rel11\'',  9286),
    ( 40, 'Multipolygon 2 holes (Tags from way)',
      'SELECT ST_NumInteriorRing(way) FROM planet_osm_polygon WHERE osm_id = -3 and landuse = \'residential\' and name = \'Name_rel11\'', 2),
    ( 41, 'Multipolygon with touching inner ways 1 hole (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -19 and landuse = \'residential\' and name = \'Name_way8\'',  12167),
    ( 42, 'Multipolygon with touching inner ways 1 hole (Tags from way)',
      'SELECT ST_NumInteriorRing(way) FROM planet_osm_polygon WHERE osm_id = -19 and landuse = \'residential\' and name = \'Name_way8\'', 1),
    ( 43, 'Multipolygon with 2 outer ways (Tags from relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'',  17581),
    ( 44, 'Multipolygon with 2 outer ways (Tags from relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 2),
    ( 45, 'Multipolygon with 2 outer ways (multigeometry)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 1),
    ( 46, 'Multipolygon with 2 outer ways (multigeometry)',
      'SELECT ST_NumGeometries(way) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 2),
    ( 47, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (Tags from relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  16169),
    ( 48, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (Tags from relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  2),
    ( 49, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (multigeometry)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  1),
    ( 50, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (multigeometry)',
      'SELECT ST_NumGeometries(way) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  2),
    ( 51, 'Basic hstore point count', 'SELECT count(*) FROM planet_osm_point;', 1360 ),
    ( 52, 'Basic hstore line count', 'SELECT count(*) FROM planet_osm_line;', 3323 ),
    ( 53, 'Basic hstore road count', 'SELECT count(*) FROM planet_osm_roads;', 375 ),
    ( 54, 'Basic hstore polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4128 ),
    ( 55, 'Basic post-diff point count', 'SELECT count(*) FROM planet_osm_point;', 1475 ),
    ( 56, 'Basic post-diff line count', 'SELECT count(*) FROM planet_osm_line;', 3367 ),
    ( 57, 'Basic post-diff road count', 'SELECT count(*) FROM planet_osm_roads;', 381 ),
    ( 58, 'Basic post-diff polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4275 ),
    ( 59, 'Extra hstore full tags point count',
      'SELECT count(*) FROM planet_osm_point WHERE tags ? \'osm_user\' and tags ? \'osm_version\' and tags ? \'osm_uid\' and tags ? \'osm_changeset\'', 1360),
    ( 60, 'Extra hstore full tags line count',
      'SELECT count(*) FROM planet_osm_line WHERE tags ? \'osm_user\' and tags ? \'osm_version\' and tags ? \'osm_uid\' and tags ? \'osm_changeset\'', 3323),
    ( 61, 'Extra hstore full tags polygon count',
      'SELECT count(*) FROM planet_osm_polygon WHERE tags ? \'osm_user\' and tags ? \'osm_version\' and tags ? \'osm_uid\' and tags ? \'osm_changeset\'', 4128),
    ( 62, 'Multipolygon copying of tags from outer with extra tags on relation',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -22', 20879),
    ( 63, 'Multipolygon copying of tags from outer with extra tags on relation (abscence of way)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 84', 0),
    ( 64, 'Multipolygon non copying of tags from outer with polygon tags on relation',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -24 and "natural" = \'water\'', 18501),
    ( 65, 'Multipolygon non copying of tags from outer with polygon tags on relation (presence of way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 83 and "landuse" = \'farmland\'', 24859),
    ( 66, 'Multipolygon diff moved point of outer way case (Tags from outer way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -15 and landuse = \'residential\' and name = \'Name_way\'', 24751),
    ( 67, 'Multipolygon diff moved point of inner way case (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -1 and landuse = \'residential\' and name = \'Name_rel\'', 13949),
    ( 68, 'Multipolygon point of inner way case (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -25 and landuse = \'farmland\' and name = \'my name\'', 23886),
    ( 69, 'Multipolygon point of inner way case (Tags from relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 90', 0),
    ( 70, 'Multipolygon diff remove relation (tagged outer way gets re added)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 90 and landuse = \'farmland\'', 32626),
    ( 71, 'Multipolygon diff remove relation',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -25', 0),
    ( 72, 'Multipolygon tags on both inner and outer (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -34 and "natural" = \'water\'', 15246),
    ( 73, 'Multipolygon tags on both inner and outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 113', 0),
    ( 74, 'Multipolygon tags on both inner and outer (abscence of inner)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 118', 0),
    ( 75, 'Multipolygon tags on both inner and outer diff change outer (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -34 and "landuse" = \'farmland\'', 15246),
    ( 76, 'Multipolygon tags on both inner and outer diff change outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 113', 0),
    ( 77, 'Multipolygon tags on both inner and outer diff change on outer (creation of inner)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 118 and "natural" = \'water\'', 1234),
    ( 78, 'Multipolygon tags on outer (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -33 and "natural" = \'water\'', 15612),
    ( 79, 'Multipolygon tags on outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 114', 0),
    ( 80, 'Multipolygon tags on outer change of way tags (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -33 and "landuse" = \'cemetery\'', 15612),
    ( 81, 'Multipolygon tags on outer (abscence of old relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -33 and "natural" = \'water\'', 0),
    ( 82, 'Multipolygon tags on relation two outer (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -29 and "natural" = \'water\'', 68492),
    ( 83, 'Multipolygon tags on relation two outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 109', 0),
    ( 84, 'Multipolygon tags on relation two outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 104', 0),
    ( 85, 'Multipolygon tags on relation two outer diff delete way (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -29 and "natural" = \'water\'', 29154),
    ( 86, 'Multipolygon tags on relation two outer (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -35 and "natural" = \'water\'', 28730),
    ( 87, 'Multipolygon tags on relation two outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 107', 0),
    ( 88, 'Multipolygon tags on relation two outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 102', 0),
    ( 89, 'Multipolygon tags on relation two outer diff remove way from relation (presence of relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -35 and "natural" = \'water\'', 15736),
    ( 90, 'Multipolygon tags on relation two outer diff remove way from relation (presence of single way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 102 and "natural" = \'water\'', 12994),
    ( 91, 'Basic line length', 'SELECT round(sum(ST_Length(way))) FROM planet_osm_line;', 4269394),
    ( 92, 'Basic line length', 'SELECT round(sum(ST_Length(way))) FROM planet_osm_roads;', 2032023),
    ( 93, 'Basic number of hstore points tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_point;', 4228),
    ( 94, 'Basic number of hstore roads tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_roads;', 2316),
    ( 95, 'Basic number of hstore lines tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_line;', 10897),
    ( 96, 'Basic number of hstore polygons tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_polygon;', 9540),
    ( 97, 'Diff import number of hstore points tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_point;', 4352),
    ( 98, 'Diff import number of hstore roads tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_roads;', 2340),
    ( 99, 'Diff import number of hstore lines tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_line;', 11020),
    ( 100, 'Diff import number of hstore polygons tags', 'SELECT sum(array_length(akeys(tags),1)) FROM planet_osm_polygon;', 9834),
    #**** Tests to check if inner polygon appears when outer tags change after initially identicall inner and outer way tags in a multi-polygon ****
    #**** These tests are currently broken and noted in trac ticket #2853 ****
    ( 101, 'Multipolygon identical tags on inner and outer (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -31 and "natural" = \'heath\'', 32702),
    ( 102, 'Multipolygon identical tags on inner and outer (abscence of outer)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 120', 0),
    ( 103, 'Multipolygon identical tags on inner and outer (abscence of inner)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 112', 0),
    ( 104, 'Multipolygon identical tags on inner and outer (presence of relation), post diff',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -31 and "natural" = \'water\'', 32702),
    ( 105, 'Multipolygon identical tags on inner and outer (presece of inner)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = 112 and "natural" = \'heath\'', 1234),
    #**** Test to check that only polygon tags that are present on all outer ways get copied over to the multi-polygon relation ****
    ( 106, 'Multipolygon copy outer tags (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -38 and "natural" = \'water\'', 29340),
    ( 107, 'Multipolygon copy outer tags (absence of partial outer tags)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -38 and "natural" = \'water\' and "man_made" = \'pier\'', 0),
    ( 108, 'Multipolygon copy outer tags (absence of multi-polygon tagged outer way)',
      'SELECT count(*) FROM planet_osm_line WHERE osm_id = 134 OR osm_id = 133', 0),
    ( 109, 'Multipolygon copy outer tags (presence of additionally tagged outer way)',
      'SELECT round(sum(ST_length(way))) FROM planet_osm_line WHERE (osm_id = 136 OR osm_id = 132) AND "man_made" = \'pier\'', 407),
    ( 110, 'Multipolygon copy outer tags (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -37 and "natural" = \'water\'', 29952),
    ( 111, 'Multipolygon copy outer tags (absence of partial outer tags)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -37 and "natural" = \'water\' and "man_made" = \'pier\'', 0),
    ( 112, 'Multipolygon copy outer tags (absence of multi-polygon tagged outer way)',
      'SELECT count(*) FROM planet_osm_line WHERE osm_id = 128 OR osm_id = 125', 0),
    ( 113, 'Multipolygon copy outer tags (presence of additionally tagged outer way)',
      'SELECT round(sum(ST_length(way))) FROM planet_osm_line WHERE (osm_id = 126 OR osm_id = 124) AND "man_made" = \'pier\'', 276),
    ( 114, 'Multipolygon copy outer tags (absence of multi-polygon tagged inner way)',
      'SELECT count(*) FROM planet_osm_line WHERE osm_id = 123 OR osm_id = 121', 0),
    ( 115, 'Multipolygon copy outer tags (presence of additionally tagged inner way)',
      'SELECT round(sum(ST_length(way))) FROM planet_osm_line WHERE (osm_id = 127 OR osm_id = 122) AND "man_made" = \'pier\'', 318),
    #**** Test to check that if polygon tags are on both outer ways and relation, polygons don't get duplicated in the db ****
    ( 116, 'Multipolygon tags on both outer and relation (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -39 and "landuse" = \'forest\'', 10379),
    ( 117, 'Multipolygon tags on both outer and relation (absence of outer way)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 138', 0),
    ( 118, 'Multipolygon tags on both outer and relation with additional tags on relation (presence of relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -40 and "landuse" = \'forest\'', 12397),
    ( 119, 'Multipolygon tags on both outer and relation with additional tags on relation (absence of outer way)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 140', 0),
    ]
#****************************************************************
#****************************************************************


class NonSlimRenderingTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(BasicNonSlimTestCase("basic case",[], [0,1,2,3,10,13, 91, 92]))
        self.addTest(BasicNonSlimTestCase("slim --drop case",["--slim","--drop"], [0,1,2,3, 10, 11, 12, 13, 91, 92]))
        self.addTest(BasicNonSlimTestCase("lat lon projection",["-l"], [0,4,5,3,10, 11, 12]))
        #Failing test 3,13 due to difference in handling mixture of tags on ways and relations, where the correct behaviour is non obvious
        #self.addTest(BasicNonSlimTestCase("--tag-transform-script", ["--tag-transform-script", "style.lua"], [0,1,2,3,10,13,91,92]))
        self.addTest(BasicNonSlimTestCase("--tag-transform-script", ["--tag-transform-script", "style.lua"], [0,1,2,10,91,92]))


class SlimRenderingTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(BasicSlimTestCase("basic case", [], [0,1,2,3,13, 91, 92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Parallel processing", ["--number-processes", "8", "-C100"], [0,1,2,3,13,91,92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Parallel processing with non 100% node-cache", ["--number-processes", "8", "-C1", "--cache-strategy=dense"], [0,1,2,3,13,91,92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Parallel processing with disabled node-cache", ["-C0"], [0,1,2,3,13,91,92],[6,7,8,9]))
        # Failes to do correct error checking. This needs fixing in osm2pgsql
        # self.addTest(BasicSlimTestCase("Parallel processing with failing database conneciton (connection limit exceeded)", ["--number-processes", "32", "-C100"], [0,1,2,3],[6,7,8,9]))
        # Counts are expected to be different in hstore, needs adjusted tests
        self.addTest(BasicSlimTestCase("Hstore match only", ["-k", "--hstore-match-only"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Hstore name column", ["-z", "name:"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Hstore", ["-k"], [51,52,53,54],[55,56,57,58]))
        self.addTest(BasicSlimTestCase("Hstore all", ["-j"], [51,52,53,54,93,94,95,96],[55,56,57,58, 97, 98, 99, 100]))
        #tests dont check for osm_timestamp which is currently missing in the pbf parser
        self.addTest(BasicSlimTestCase("Extra tags hstore match only", ["-x", "-k", "--hstore-match-only"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Extra tags hstore all", ["-j", "-x"], [51,52,53,54,59,60,61],[55,56,57,58]))
        
        self.addTest(BasicSlimTestCase("--tablespace-main-data", ["--tablespace-main-data", "tablespacetest"], [0,1,2,3,13,91,92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-main-index", ["--tablespace-main-index", "tablespacetest"], [0,1,2,3,13,91,92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-slim-data", ["--tablespace-slim-data", "tablespacetest"], [0,1,2,3,13,91,92],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-slim-index", ["--tablespace-slim-index", "tablespacetest"], [0,1,2,3,13,91,92],[6,7,8,9]))
        #Failing test 3,13,9 due to difference in handling mixture of tags on ways and relations, where the correct behaviour is non obvious
        #self.addTest(BasicNonSlimTestCase("--tag-transform-script", ["--tag-transform-script", "style.lua"], [0,1,2,3,10,13,91,92]))
        self.addTest(BasicSlimTestCase("--tag-transform-script", ["--tag-transform-script", "style.lua"], [0,1,2,91,92],[6,7,8]))


class SlimGazetteerTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(BasicGazetteerTestCase("basic case", [], [14,15,16,17,22,24],[18,19,20,21,23,25]))


class MultiPolygonSlimRenderingTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        #Case 77 currently doesn't work
        self.addTest(MultipolygonSlimTestCase("basic case", [],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 68, 69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("multi geometry", ["-G"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 45, 46, 47, 49, 50, 62, 63, 64, 65, 68, 69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 45, 46, 47, 49, 50, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("hstore case", ["-k"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,47,48,62,63,64,65,68,69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("hstore case", ["-k", "--hstore-match-only"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40,41,42, 43, 44, 47, 48,  62, 63, 64, 65, 68, 69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("Extra tags hstore match only", ["-x", "-k", "--hstore-match-only"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40,41,42, 43, 44, 47, 48,  62, 63, 64, 65, 68, 69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("Extra tags hstore match only", ["-x", "-j"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40,41,42, 43, 44, 47, 48,  62, 63, 64, 65, 68, 69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,
                                               106,107,108,109,110,111,112,113,114,115,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63, 64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("lua tagtransform case", ["--tag-transform-script", "style.lua"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40, 41, 42, 43, 44, 47, 48,  62, 64, 65,68,69, 72, 73, 74, 78, 79, 82, 83, 84, 86, 87, 88,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48, 62, 63,64, 65, 66, 67, 70, 71, 75, 76, 79, 80, 81, 83, 84, 85, 87, 89, 90]))
        self.addTest(MultipolygonSlimTestCase("lua tagtransform case with hstore", ["--tag-transform-script", "style.lua", "-k"],
                                              [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,47,48,62,63,64,65,68,69,72,73,74,78,79,82,83,84,86,87,88,116,117,118,119],
                                              [28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,47,48,62,63,64,65,66,67,70,71,75,76,79,80,81,83,84,85,87,89,90]))


class CompleteTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self, map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(NonSlimRenderingTestSuite())
        self.addTest(SlimRenderingTestSuite())
        self.addTest(MultiPolygonSlimRenderingTestSuite())
        self.addTest(SlimGazetteerTestSuite())

#****************************************************************
class ThirdTestCase(unittest.TestCase):
    def testOne(self):
        assert 1 == 1
    def testTwo(self):
        assert 2 == 2

#****************************************************************

class BaseTestCase(unittest.TestCase):
    def dbConnect(self):
        try:
            self.conn=psycopg2.connect("dbname='osm2pgsql-test'")
            self.conn.autocommit = True
            self.cur = self.conn.cursor()
        except Exception, e:
            print "I am unable to connect to the database." + e

    def dbClose(self):
        self.cur.close()
        self.conn.close()

    def executeStatements(self, seq):
        print "*********************************"
        self.dbConnect()
        try:
            for i in seq:
                self.assertEqual(sql_test_statements[i][0], i, "test case numbers don't match up: " + str(i) + " =/=" + str(sql_test_statements[i][0]))
                try:
                    self.cur.execute(sql_test_statements[i][2])
                    res = self.cur.fetchall()
                except Exception, e:
                    self.assertEqual(0, 1, str(sql_test_statements[i][0]) + ": Failed to execute " + sql_test_statements[i][1] +
                                     " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
                if (res == None):
                        self.assertEqual(0, 1, str(sql_test_statements[i][0]) + ": Sql statement returned no results: " +
                                         sql_test_statements[i][1] + " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
                self.assertEqual(len(res), 1, str(sql_test_statements[i][0]) + ": Sql statement returned more than one result: " +
                                 str(res) + "  -- " + sql_test_statements[i][1] + " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
                self.assertEqual( res[0][0], sql_test_statements[i][3],
                                  str(sql_test_statements[i][0]) + ": Failed " + sql_test_statements[i][1] + ", expected " + str(sql_test_statements[i][3]) + " but was " + str(res[0][0]) +
                                  " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
        finally:
            self.dbClose()

#****************************************************************

class BaseNonSlimTestCase(BaseTestCase):
    
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [full_import_file])
        self.assertEqual( returncode, 0, "Execution of osm2pgsql with options: " + str(parameters) + " failed")

class BaseSlimTestCase(BaseTestCase):    
        
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])
        self.assertEqual( returncode, 0, "Execution of osm2pgsql --slim with options: " + str(parameters) + " failed")

    def updateGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "--append", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])
        self.assertEqual( returncode, 0, "Execution of osm2pgsql --slim --append with options: " + str(parameters) + " failed")

class BaseGazetteerTestCase(BaseTestCase):    
        
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Ogazetteer", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])
        self.assertEqual( returncode, 0, "Execution of osm2pgsql --slim -Ogazetteer with options: " + str(parameters) + " failed")

    def updateGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Ogazetteer", "--append", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])
        self.assertEqual( returncode, 0, "Execution of osm2pgsql --slim -Ogazetteer --append with options: " + str(parameters) + " failed")

#****************************************************************
class BasicNonSlimTestCase(BaseNonSlimTestCase):

    def __init__(self, name, parameters, initialStatements):
        BaseNonSlimTestCase.__init__(self)
        self.name = name
        self.parameters = parameters
        self.initialStatements = initialStatements

    def setUp(self):
        self.setUpGeneric(self.parameters, full_import_file)

    def runTest(self):
        print "****************************************"
        print "Running initial import for " + self.name
        self.executeStatements(self.initialStatements)


class BasicSlimTestCase(BaseSlimTestCase):

    def __init__(self, name, parameters, initialStatements, postDiffStatements):
        BaseSlimTestCase.__init__(self)
        self.name = name
        self.parameters = parameters
        self.initialStatements = initialStatements
        self.postDiffStatements = postDiffStatements

    def setUp(self):
        self.setUpGeneric(self.parameters, full_import_file)


    def runTest(self):
        print "****************************************"
        print "Running initial import for " + self.name
        self.executeStatements(self.initialStatements)
        print "Running diff-import for " + self.name
        self.updateGeneric(self.parameters, diff_import_file)
        self.executeStatements(self.postDiffStatements)

class MultipolygonSlimTestCase(BaseSlimTestCase):

    def __init__(self, name, parameters, initialStatements, postDiffStatements):
        BaseSlimTestCase.__init__(self)
        self.name = name
        self.parameters = parameters
        self.initialStatements = initialStatements
        self.postDiffStatements = postDiffStatements

    def setUp(self):
        self.setUpGeneric(self.parameters, multipoly_import_file)


    def runTest(self):
        print "****************************************"
        print "Running initial import for " + self.name
        self.executeStatements(self.initialStatements)
        print "Running diff-import for " + self.name
        self.updateGeneric(self.parameters, diff_multipoly_import_file)
        self.executeStatements(self.postDiffStatements)


class BasicGazetteerTestCase(BaseGazetteerTestCase):

    def __init__(self, name, parameters, initialStatements, postDiffStatements):
        BaseGazetteerTestCase.__init__(self)
        self.name = name
        self.parameters = parameters
        self.initialStatements = initialStatements
        self.postDiffStatements = postDiffStatements

    def setUp(self):
        self.setUpGeneric(self.parameters, full_import_file)


    def runTest(self):
        print "****************************************"
        print "Running initial import in gazetteer mode for " + self.name
        self.executeStatements(self.initialStatements)
        print "Running diff-import in gazetteer mode for " + self.name
        self.updateGeneric(self.parameters, diff_import_file)
        self.executeStatements(self.postDiffStatements)



#****************************************************************
#****************************************************************
def setupDB():
    print "Setting up test database"
    try:
        gen_conn=psycopg2.connect("dbname='template1'")
        gen_conn.autocommit = True
    except Exception, e:
        print "I am unable to connect to the database."
        exit()

    try:
        gen_cur = gen_conn.cursor()
    except Exception, e:
        gen_conn.close()
        print "I am unable to connect to the database."
        exit()

    try:
        gen_cur.execute("""DROP DATABASE IF EXISTS \"osm2pgsql-test\"""")
        gen_cur.execute("""CREATE DATABASE \"osm2pgsql-test\" WITH ENCODING 'UTF8'""")
    except Exception, e:
        print "Failed to create osm2pgsql-test db" + e.pgerror
        exit();
    finally:
        gen_cur.close()
        gen_conn.close()

    try:
        test_conn=psycopg2.connect("dbname='osm2pgsql-test'")
        test_conn.autocommit = True
    except Exception, e:
        print "I am unable to connect to the database." + e
        exit()

    try:
        test_cur = test_conn.cursor()
    except Exception, e:
        print "I am unable to connect to the database." + e
        gen_conn.close()
        exit()

    try:
        try:
            global created_tablespace
            test_cur.execute("""SELECT spcname FROM pg_tablespace WHERE spcname = 'tablespacetest'""")
            if test_cur.fetchone():
                print "We already have a tablespace, can use that"
                created_tablespace = 0
            else:
                print "For the test, we need to create a tablespace. This needs root privilidges"
                created_tablespace = 1
                ### This makes postgresql read from /tmp
                ## Does this have security implications like opening this to a possible symlink attack?
                try:
                    os.mkdir("/tmp/psql-tablespace")
                    returncode = subprocess.call(["/usr/bin/sudo", "/bin/chown", "postgres.postgres", "/tmp/psql-tablespace"])
                    test_cur.execute("""CREATE TABLESPACE tablespacetest LOCATION '/tmp/psql-tablespace'""")
                except Exception, e:
                    os.rmdir("/tmp/psql-tablespace")
                    self.assertEqual(0, 1, "Failed to create tablespace")
        except Exception, e:
            print "Failed to create directory for tablespace" + str(e)


        try:
            test_cur.execute("""CREATE EXTENSION postgis;""")
        except:
            test_conn.rollback()
            # Guess the directory from the postgres version.
            # TODO: make the postgisdir configurable. Probably
            # only works on Debian-based distributions at the moment.
            postgisdir = ('/usr/share/postgresql/%d.%d/contrib' %
                        (test_conn.server_version / 10000, (test_conn.server_version / 100) % 100))
            for fl in os.listdir(postgisdir):
                if fl.startswith('postgis'):
                    newdir = os.path.join(postgisdir, fl)
                    if os.path.isdir(newdir):
                        postgisdir = newdir
                        break
            else:
                raise Exception('Cannot find postgis directory.')
            pgscript = open(os.path.join(postgisdir, 'postgis.sql'),'r').read()
            test_cur.execute(pgscript)
            pgscript = open(os.path.join(postgisdir, 'spatial_ref_sys.sql'), 'r').read()
            test_cur.execute(pgscript)

        try:
            test_cur.execute("""CREATE EXTENSION hstore;""")

        except Exception, e:
            print "I am unable to create extensions: " + e.pgerror
            exit()
    finally:
        test_cur.close()
        test_conn.close()

def tearDownDB():
    print "Cleaning up test database"
    try:
        gen_conn=psycopg2.connect("dbname='template1'")
        gen_conn.autocommit = True
        gen_cur = gen_conn.cursor()
    except Exception, e:
        print "I am unable to connect to the database."
        exit()

    try:
        gen_cur.execute("""DROP DATABASE IF EXISTS \"osm2pgsql-test\"""")
        if (created_tablespace == 1):
            gen_cur.execute("""DROP TABLESPACE IF EXISTS \"tablespacetest\"""")
    except Exception, e:
        print "Failed to clean up osm2pgsql-test db" + e.pgerror
        exit();

    gen_cur.close()
    gen_conn.close()
    if (created_tablespace == 1):
        returncode = subprocess.call(["/usr/bin/sudo", "/bin/rmdir", "/tmp/psql-tablespace"])



ts2 = CompleteTestSuite()
try:
    setupDB()
    runner = unittest.TextTestRunner()
    runner.run(ts2)
finally:
    tearDownDB()
