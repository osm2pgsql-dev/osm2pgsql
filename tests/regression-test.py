import unittest
import psycopg2
import os
from pwd import getpwnam
import subprocess

full_import_file="tests/liechtenstein-2013-08-03.osm.pbf"
multipoly_import_file="tests/test_multipolygon.osm" #This file contains a number of different multi-polygon test cases
diff_import_file="tests/000466354.osc.gz"

created_tablespace = 0

#****************************************************************
#****************************************************************
sql_test_statements=[
    ( 0, 'Basic point count', 'SELECT count(*) FROM planet_osm_point;', 1342 ),
    ( 1, 'Basic line count', 'SELECT count(*) FROM planet_osm_line;', 3300 ),
    ( 2, 'Basic road count', 'SELECT count(*) FROM planet_osm_roads;', 375 ),
    ( 3, 'Basic polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4127 ),
    ( 4,  'Basic latlon line count', 'SELECT count(*) FROM planet_osm_line;', 3298 ),
    ( 5, 'Basic latlon road count', 'SELECT count(*) FROM planet_osm_roads;', 374 ),
    ( 6, 'Basic post-diff point count', 'SELECT count(*) FROM planet_osm_point;', 1457 ),
    ( 7, 'Basic post-diff line count', 'SELECT count(*) FROM planet_osm_line;', 3344 ),
    ( 8, 'Basic post-diff road count', 'SELECT count(*) FROM planet_osm_roads;', 381 ),
    ( 9, 'Basic post-diff polygon count', 'SELECT count(*) FROM planet_osm_polygon;', 4274 ),
    ( 10, 'Absence of nodes table', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_nodes\'', 0),
    ( 11, 'Absence of way table', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_ways\'', 0),
    ( 12, 'Absence of rel line', 'SELECT count(*) FROM pg_tables WHERE tablename = \'planet_osm_rels\'', 0),
    ( 13, 'Basic polygon area', 'SELECT round(cast(ST_Area(way) as numeric),0) FROM planet_osm_polygon;', 5138688),
    ( 14, 'Gazetteer place count', 'SELECT count(*) FROM place', 4709),
    ( 15, 'Gazetteer place node count', 'SELECT count(*) FROM place WHERE osm_type = \'N\'', 988),
    ( 16, 'Gazetteer place way count', 'SELECT count(*) FROM place WHERE osm_type = \'W\'', 3699),
    ( 17, 'Gazetteer place rel count', 'SELECT count(*) FROM place WHERE osm_type = \'R\'', 22),
    ( 18, 'Gazetteer post-diff place count', 'SELECT count(*) FROM place', 4765),
    ( 19, 'Gazetteer post-diff place node count', 'SELECT count(*) FROM place WHERE osm_type = \'N\'', 999),
    ( 20, 'Gazetteer post-diff place way count', 'SELECT count(*) FROM place WHERE osm_type = \'W\'', 3744),
    ( 21, 'Gazetteer post-diff place rel count', 'SELECT count(*) FROM place WHERE osm_type = \'R\'', 22),
    ( 22, 'Gazetteer housenumber count', 'SELECT count(*) FROM place WHERE housenumber is not null', 199),
    ( 23, 'Gazetteer post-diff housenumber count count', 'SELECT count(*) FROM place WHERE housenumber is not null', 199),
    ( 24, 'Gazetteer isin count', 'SELECT count(*) FROM place WHERE isin is not null', 239),
    ( 25, 'Gazetteer post-diff isin count count', 'SELECT count(*) FROM place WHERE isin is not null', 239),
    ( 26, 'Multipolygon basic case (Tags from outer way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -15 and landuse = \'residential\' and name = \'Name_way\'', 12894),
    ( 27, 'Multipolygon basic case (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -1 and landuse = \'residential\' and name = \'Name_rel\'', 12894),
    ( 28, 'Multipolygon named inner - outer (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -16 and landuse = \'residential\' and name = \'Name_way2\'', 12893),
    ( 29, 'Multipolygon named inner - inner way',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 4 and landuse = \'farmland\' and name = \'Name_way3\'', 3144),
    ( 30, 'Multipolygon named inner - outer (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -8 and landuse = \'residential\' and name = \'Name_rel2\'', 12893),
    ( 31, 'Multipolygon named inner - inner way',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = 5 and landuse = \'farmland\' and name = \'Name_way4\'', 3144),
    ( 32, 'Multipolygon named same inner - outer (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -17 and landuse = \'residential\' and name = \'Name_way16\'', 12894),
    ( 33, 'Multipolygon named same inner - inner way absent',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = 15', 0),
    ( 34, 'Multipolygon non-area inner - outer (Tags from relation)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -14 and landuse = \'residential\' and name = \'Name_way5\'', 12894),
    ( 35, 'Multipolygon non-area inner - inner (Tags from way)',
      'SELECT round(ST_Length(way)) FROM planet_osm_line WHERE osm_id = 6 and highway = \'residential\' and name = \'Name_way6\'', 228),
    ( 36, 'Multipolygon 2 holes (Tags from way)',
      'SELECT round(ST_Area(way)) FROM planet_osm_polygon WHERE osm_id = -18 and landuse = \'residential\' and name = \'Name_way7\'', 11822),
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
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'',  17582),
    ( 44, 'Multipolygon with 2 outer ways (Tags from relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 2),
    ( 45, 'Multipolygon with 2 outer ways (multigeometry)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 1),
    ( 46, 'Multipolygon with 2 outer ways (multigeometry)',
      'SELECT ST_NumGeometries(way) FROM planet_osm_polygon WHERE osm_id = -13 and landuse = \'farmland\' and name = \'Name_rel9\'', 2),
    ( 47, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (Tags from relation)',
      'SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  16168),
    ( 48, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (Tags from relation)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  2),
    ( 49, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (multigeometry)',
      'SELECT count(*) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  1),
    ( 50, 'Multipolygon nested outer ways. Both outer and inner ways are from multiple ways (multigeometry)',
      'SELECT ST_NumGeometries(way) FROM planet_osm_polygon WHERE osm_id = -7 and landuse = \'farmland\' and name = \'Name_rel15\'',  2),
    
    
    
    ]
#****************************************************************
#****************************************************************


class NonSlimRenderingTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(BasicNonSlimTestCase("basic case",[], [0,1,2,3,10,13]))
        self.addTest(BasicNonSlimTestCase("slim --drop case",["--slim","--drop"], [0,1,2,3, 10, 11, 12, 13]))
        self.addTest(BasicNonSlimTestCase("lat lon projection",["-l"], [0,4,5,3,10, 11, 12]))


class SlimRenderingTestSuite(unittest.TestSuite):
    def __init__(self):
        unittest.TestSuite.__init__(self,map(ThirdTestCase,
                                             ("testOne",
                                              "testTwo")))
        self.addTest(BasicSlimTestCase("basic case", [], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("Parallel processing", ["--number-processes", "8", "-C100"], [0,1,2,3],[6,7,8,9]))
        # Failes to do correct error checking. This needs fixing in osm2pgsql
        # self.addTest(BasicSlimTestCase("Parallel processing with failing database conneciton (connection limit exceeded)", ["--number-processes", "32", "-C100"], [0,1,2,3],[6,7,8,9]))
        # Counts are expected to be different in hstore, needs adjusted tests
        # self.addTest(BasicSlimTestCase("Hstore", ["-k"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-main-data", ["--tablespace-main-data", "tablespacetest"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-main-index", ["--tablespace-main-index", "tablespacetest"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-slim-data", ["--tablespace-slim-data", "tablespacetest"], [0,1,2,3],[6,7,8,9]))
        self.addTest(BasicSlimTestCase("--tablespace-slim-index", ["--tablespace-slim-index", "tablespacetest"], [0,1,2,3],[6,7,8,9]))
        # Lua processing is not exactly equivalent at the moment, so counts are differetn. Needs fixing
        #self.addTest(BasicSlimTestCase("--tag-transform-script", ["--tag-transform-script", "style.lua"], [0,1,2,3],[6,7,8,9]))


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
        self.addTest(MultipolygonSlimTestCase("basic case", [], [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 44, 47, 48],[]))
        self.addTest(MultipolygonSlimTestCase("multi geometry", ["-G"], [26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42, 43, 45, 46, 47, 49, 50],[]))
        self.addTest(MultipolygonSlimTestCase("hstore case", ["-k"], [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40,41,42, 43, 44, 47, 48],[]))
        self.addTest(MultipolygonSlimTestCase("lua tagtransform case", ["--tag-transform-script", "style.lua"], [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40, 41, 42, 43, 44, 47, 48],[]))
        self.addTest(MultipolygonSlimTestCase("lua tagtransform case with hstore", ["--tag-transform-script", "style.lua", "-k"], [26,27,28,29,30,31,32,33,34,35,36,37,38, 39, 40, 41, 42, 43, 44, 47, 48],[]))


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
                self.assertEqual(sql_test_statements[i][0], i, "test case numbers don't match up")
                try:
                    self.cur.execute(sql_test_statements[i][2])
                    res = self.cur.fetchone()
                except Exception, e:
                    self.assertEqual(0, 1, "Failed to execute " + sql_test_statements[i][1] + " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
                self.assertEqual( res[0], sql_test_statements[i][3],
                                  "Failed " + sql_test_statements[i][1] + ", expected " + str(sql_test_statements[i][3]) + " but was " + str(res[0]) +
                                  " (" + sql_test_statements[i][2] + ") {" + str(self.parameters) +"}")
        finally:
            self.dbClose()

#****************************************************************

class BaseNonSlimTestCase(BaseTestCase):
    
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [full_import_file])

class BaseSlimTestCase(BaseTestCase):    
        
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])

    def updateGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "--append", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])

class BaseGazetteerTestCase(BaseTestCase):    
        
    def setUpGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Ogazetteer", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])

    def updateGeneric(self, parameters, file):
        returncode = subprocess.call(["./osm2pgsql", "--slim", "-Ogazetteer", "--append", "-Sdefault.style", "-dosm2pgsql-test", "-C100"] + parameters + [file])

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
