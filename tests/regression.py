""" A collection of integration tests.

    The tests are organised as follows:

    The global module setup creates a test database which is used by all
    tests in this test suite.

    Each TestCase class represents one import/update run of osm2pgsql. The
    import is run in the class setup method of the class. The tests themself
    then check the contents of the resulting database. Each test executes
    exactly one SQL statement, so that always all checks are run on each
    import regardless of the result of the other checks.

    The tests are organised in groups, each of which is implemented in its
    own class (called *Tests). Each TestCase defines the arguments with which
    to call osm2pgsql and then inherits from all test groups it wants executed.
"""

import logging
from os import path as op
import os
import subprocess
import sys
import psycopg2
import unittest

# Base configuration for the tests. May be overwritten by the importer.
CONFIG = {
    'executable' : './osm2pgsql',
    'test_database' : 'osm2pgsql-test',
    'test_data_path' : 'tests/data',
    'default_data_path' : '.',
    'have_lua' : True,
    'have_tablespace' : True
}

#######################################################################
#
#  Global setup and teardown.

def setUpModule():
    """ Create a database that is used for all tests.
    """
    loglevel = logging.ERROR
    for arg in sys.argv:
        if arg[0] == '-' and arg[1:] == (len(arg) - 1) * 'v':
            loglevel -= (len(arg) - 1) * 10

    logging.basicConfig(level=loglevel if loglevel > 0 else 1)
    logging.info("Setting up test database")
    dbname = CONFIG['test_database']

    with psycopg2.connect("dbname='template1'") as conn:
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute('DROP DATABASE IF EXISTS "{}"'.format(dbname))
            cur.execute("CREATE DATABASE \"{}\" WITH ENCODING 'UTF8'"
                        .format(dbname))

    with psycopg2.connect("dbname='{}'".format(dbname)) as conn:
        conn.autocommit = True
        with conn.cursor() as cur:
            # Check if there is a dataspace, we will skip tests otherwise.
            if CONFIG['have_tablespace']:
                cur.execute("""SELECT spcname FROM pg_tablespace
                               WHERE spcname = 'tablespacetest'""")
                CONFIG['have_tablespace'] = cur.fetchone()

            cur.execute('CREATE EXTENSION postgis')
            cur.execute('CREATE EXTENSION hstore')

def tearDownModule():
    """ Destroy the global database.
    """
    logging.info("Cleaning up test database")

    with psycopg2.connect("dbname='template1'") as conn:
        with conn.cursor() as cur:
            conn.autocommit = True
            cur.execute('DROP DATABASE IF EXISTS "{}"'
                        .format(CONFIG['test_database']))

########################################################################
#
# Runners
#
# These provide the boiler plate for importing and updating the database.

class BaseRunner(object):
    """ The base class for all runners. Provides the setup function that
        imports and optionally updates the database. It also implements
        convenience functions for the database checks.
    """

    conn = None
    extra_params = None
    use_gazetteer = False
    use_lua_tagtransform = False
    import_file = None
    update_file = None
    schema = None

    @classmethod
    def setUpClass(cls):
        try:
            # In case one is left over from a previous test
            os.remove('flat.nodes')
        except (FileNotFoundError):
            pass
        if cls.use_lua_tagtransform and not CONFIG['have_lua']:
            cls.skipTest(None, "No Lua configured.")
        if 'tablespacetest' in cls.extra_params and not CONFIG['have_lua']:
            cls.skipTest(None, "Test tablespace 'tablespacetest' not configured.")
        with psycopg2.connect("dbname='{}'".format(CONFIG['test_database'])) as conn:
            with conn.cursor() as cur:
                for t in ('nodes', 'ways', 'rels', 'point', 'line', 'roads', 'polygon'):
                    cur.execute("DROP TABLE IF EXISTS planet_osm_" + t)
                cur.execute("DROP SCHEMA IF EXISTS osm CASCADE")
                if cls.schema:
                    cur.execute("CREATE SCHEMA " + cls.schema)

        if cls.import_file:
            cls.run_import(cls.get_def_params() + cls.extra_params,
                            op.join(CONFIG['test_data_path'], cls.import_file))
        if cls.update_file:
            cls.run_import(['-a'] + cls.get_def_params() + cls.extra_params,
                           op.join(CONFIG['test_data_path'], cls.update_file))

        BaseRunner.conn = psycopg2.connect("dbname='{}'".format(CONFIG['test_database']))

    @classmethod
    def tearDownClass(cls):
        if BaseRunner.conn:
            BaseRunner.conn.close()
            BaseRunner.conn = None
        if cls.schema:
            with psycopg2.connect("dbname='{}'".format(CONFIG['test_database'])) as conn:
                conn.cursor().execute("DROP SCHEMA IF EXISTS {} CASCADE".format(cls.schema))
        try:
            os.remove('flat.nodes')
        except (FileNotFoundError):
            pass

    @classmethod
    def get_def_params(cls):
        if cls.use_gazetteer:
            params = ['-S',
                      op.join(CONFIG['test_data_path'], 'gazetteer-test.style'),
                      '-O', 'gazetteer']
        else:
            params = ['-S',
                        op.join(CONFIG['default_data_path'], 'default.style')]
        if cls.use_lua_tagtransform:
            params += ['--tag-transform-script',
                       op.join(CONFIG['default_data_path'], 'style.lua')]

        return params

    @classmethod
    def run_import(cls, params, filename):
        cmdline = [CONFIG['executable']]
        if not '-d' in params and not '--database' in params:
            cmdline.extend(('-d', CONFIG['test_database']))
        cmdline.extend(params)
        cmdline.append(op.join(CONFIG['test_data_path'], filename))
        logging.info("Executing command: {}".format(' '.join(cmdline)))

        proc = subprocess.Popen(cmdline,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        (outp, err) = proc.communicate()

        if proc.returncode == 0:
            logging.debug(err.decode('utf-8'))
        else:
            logging.warning(err.decode('utf-8'))
            raise RuntimeError("Import failed.")


    def assert_count(self, count, table, where=None):
        if self.schema:
            table = self.schema + '.' + table
        sql = 'SELECT count(*) FROM ' + table
        if where:
            sql += ' WHERE ' + where
        self.assert_sql(count, sql)

    def assert_sql(self, expect, sql):
        with BaseRunner.conn.cursor() as cur:
            cur.execute(sql)
            self.assertEqual(cur.rowcount, 1)
            self.assertEqual(expect, cur.fetchone()[0])


class BaseImportRunner(BaseRunner):
    import_file = 'liechtenstein-2013-08-03.osm.pbf'
    update = False


class BaseUpdateRunner(BaseRunner):
    import_file = 'liechtenstein-2013-08-03.osm.pbf'
    update_file = '000466354.osc.gz'
    update = True


class MultipolygonImportRunner(BaseRunner):
    import_file = 'test_multipolygon.osm'
    update = False


class MultipolygonUpdateRunner(BaseRunner):
    import_file = 'test_multipolygon.osm'
    update_file = 'test_multipolygon_diff.osc'
    update = True


class BaseUpdateRunnerWithOutputSchema(BaseUpdateRunner):
    schema = 'osm'


class DependencyUpdateRunner(BaseRunner):
    import_file = 'test_forward_dependencies.opl'
    update_file = 'test_forward_dependencies_diff.opl'


########################################################################
#
# Test groups
#
# Classes with test that check the content of the database.

class PgsqlBaseTests(object):
    """ Tests the basic counts of a Liechtenstein import with the
        pgsql output.
    """
    def is_full_hstore(self):
        return (('--hstore' in self.extra_params
                or '-k' in self.extra_params or '-j' in self.extra_params)
               and '--hstore-match-only'not in self.extra_params)

    def test_point_count(self):
        if self.is_full_hstore():
            count = 1475 if self.update else 1360
        else:
            count = 1457 if self.update else 1342

        self.assert_count(count, 'planet_osm_point')

    def test_line_count(self):
        if self.is_full_hstore():
            count = 3297 if self.update else 3254
        elif '-l' in self.extra_params:
            count = 3274 if self.update else 3229
        else:
            count = 3274 if self.update else 3231

        self.assert_count(count, 'planet_osm_line')

    def test_road_count(self):
        if '-l' in self.extra_params:
            count = 380 if self.update else 374
        else:
            count = 380 if self.update else 375

        self.assert_count(count, 'planet_osm_roads')

    def test_polygon_count(self):
        if self.is_full_hstore():
            count = 4278 if self.update else 4131
        elif self.use_lua_tagtransform:
            # There is a difference in handling mixture of tags on ways and
            # relations with Lua tagtransform, where the correct behaviour is
            # non-obvious.
            count = 4283 if self.update else 4136
        else:
            count = 4277 if self.update else 4130

        self.assert_count(count, 'planet_osm_polygon')


class PgsqlMercGeomTests(object):
    """ Tests that mercator-transformed geometries are plausible.
    """
    def test_basic_polygon_area(self):
        area = 1272140688 if self.use_lua_tagtransform else 1247245186
        self.assert_sql(area,
                        """SELECT round(sum(cast(ST_Area(way) as numeric)),0)
                           FROM planet_osm_polygon""")

    def test_basic_line_length(self):
        self.assert_sql(4211350,
                        'SELECT round(sum(ST_Length(way))) FROM planet_osm_line')

    def test_basic_road_length(self):
        self.assert_sql(2032023,
                        'SELECT round(sum(ST_Length(way))) FROM planet_osm_roads')


class DroppedTableTests(object):
    """ Tests for the absence of middle tables."""
    def test_absence_nodes_table(self):
        self.assert_count(0, 'pg_tables',
                          where="tablename = 'planet_osm_nodes'")

    def test_absence_way_table(self):
        self.assert_count(0, 'pg_tables',
                          where="tablename = 'planet_osm_ways'")

    def test_absence_rel_table(self):
        self.assert_count(0, 'pg_tables',
                          where="tablename = 'planet_osm_rels'")


class ExtraTagTests(object):
    """ Tests for presence of extra meta tags.
    """
    def test_metatags_point(self):
        self.assert_count(1360, 'planet_osm_point',
                          """tags ? 'osm_user' AND tags ? 'osm_version'
                             AND tags ? 'osm_uid' AND tags ? 'osm_changeset'""")

    def test_metatags_line(self):
        self.assert_count(3254, 'planet_osm_line',
                          """tags ? 'osm_user' AND tags ? 'osm_version'
                             AND tags ? 'osm_uid' AND tags ? 'osm_changeset'""")

    def test_metatags_polygon(self):
        self.assert_count(4131, 'planet_osm_polygon',
                          """tags ? 'osm_user' AND tags ? 'osm_version'
                             AND tags ? 'osm_uid' AND tags ? 'osm_changeset'""")

class HstoreTagTests(object):
    """ Tests for entries in hstore columns in --hstore-all mode.
    """
    def test_number_hstore_tags_points(self):
        self.assert_sql(4352 if self.update else 4228,
                        """SELECT sum(array_length(akeys(tags),1))
                           FROM planet_osm_point""")

    def test_number_hstore_tags_roads(self):
        self.assert_sql(2336 if self.update else 2317,
                        """SELECT sum(array_length(akeys(tags),1))
                           FROM planet_osm_roads""")

    def test_number_hstore_tags_lines(self):
        self.assert_sql(10505 if self.update else 10387,
                        """SELECT sum(array_length(akeys(tags),1))
                           FROM planet_osm_line""")

    def test_number_hstore_tags_polygons(self):
        self.assert_sql(9832 if self.update else 9538,
                        """SELECT sum(array_length(akeys(tags),1))
                           FROM planet_osm_polygon""")

class GazetteerTests(object):
    """ Tests for gazetteer output using Liechtenstein file.
    """
    def test_place_count(self):
        self.assert_count(2877 if self.update else 2836, 'place')

    def test_place_node_count(self):
        self.assert_count(764 if self.update else 759, 'place',
                          where="osm_type = 'N'")

    def test_place_way_count(self):
        self.assert_count(2095 if self.update else 2059, 'place',
                          where="osm_type = 'W'")

    def test_place_rel_count(self):
        self.assert_count(18 if self.update else 18, 'place',
                          where="osm_type = 'R'")

    def test_place_housenumber_count(self):
        self.assert_count(199, 'place', where="address ? 'housenumber'")

    def test_place_address_count(self):
        self.assert_count(319, 'place', where='address is not null')


class MultipolygonTests(object):
    """ Tests for the special multipolygon input.
    """
    def assert_area(self, area, osm_id, k, v, name = None):
        sql = """SELECT round(ST_Area(way)) FROM planet_osm_polygon
                 WHERE osm_id = {} AND "{}" = '{}'""".format(osm_id, k, v)
        if name is not None:
            sql += " AND name = '{}'".format(name)
        self.assert_sql(area, sql)

    def assert_length(self, length, osm_id, k, v, name):
        sql = """SELECT round(ST_Length(way)) FROM planet_osm_line
                 WHERE osm_id = {} AND "{}" = '{}' AND name = '{}'"""
        self.assert_sql(length, sql.format(osm_id, k, v, name))

    def assert_inner_rings(self, num, osm_id, k, v, name):
        sql = """SELECT round(ST_NumInteriorRing(way)) FROM planet_osm_polygon
                 WHERE osm_id = {} AND "{}" = '{}' AND name = '{}'"""
        self.assert_sql(num, sql.format(osm_id, k, v, name))

    def assert_sum_area(self, area, osm_id, k, v, name=None):
        sql = """SELECT round(sum(ST_Area(way))) FROM planet_osm_polygon
                 WHERE osm_id = {} AND "{}" = '{}'"""
        if name is not None:
            sql += " AND name = '{}'".format(name)
        self.assert_sql(area, sql.format(osm_id, k, v, name))

    def assert_num_rows(self, area, osm_id, k, v, name = None):
        sql = """SELECT count(*) FROM planet_osm_polygon
                 WHERE osm_id = {} AND "{}" = '{}'"""
        if name is not None:
            sql += " AND name = '{}'".format(name)
        self.assert_sql(area, sql.format(osm_id, k, v, name))

    def assert_num_geoms(self, area, osm_id, k, v, name):
        sql = """SELECT ST_NumGeometries(way) FROM planet_osm_polygon
                 WHERE osm_id = {} AND "{}" = '{}' AND name = '{}'"""
        self.assert_sql(area, sql.format(osm_id, k, v, name))


    def test_tags_from_relation(self):
        self.assert_area(13949 if self.update else 12895,
                         -1, 'landuse', 'residential', 'Name_rel')

    def test_named_inner_inner_way(self):
        self.assert_area(3144, 4, 'landuse', 'farmland', 'Name_way3')

    def test_named_inner_outer_way(self):
        self.assert_area(12894, -8, 'landuse', 'residential', 'Name_rel2')

    def test_named_inner_outer_way_2(self):
        self.assert_area(3144, 5, 'landuse', 'farmland', 'Name_way4')

    def test_nonarea_inner_outer(self):
        self.assert_area(12894, -14, 'landuse', 'residential', 'Name_way5')

    def test_nonarea_inner_inner(self):
        self.assert_length(228, 6, 'highway', 'residential', 'Name_way6')

    def test_multipolygon_no_holes(self):
        self.assert_area(11529, -11, 'landuse', 'residential', 'Name_rel6')

    def test_multipolygon_two_holes_area(self):
        self.assert_area(9286, -3, 'landuse', 'residential', 'Name_rel11')

    def test_multipolygon_two_holes_rings(self):
        self.assert_inner_rings(2, -3, 'landuse', 'residential', 'Name_rel11')

    def test_multipolygon_two_outer(self):
        self.assert_sum_area(17581, -13, 'landuse', 'farmland', 'Name_rel9')

    def test_multipolygon_two_outer_num(self):
        if '-G' in self.extra_params:
            self.assert_num_geoms(2, -13, 'landuse', 'farmland', 'Name_rel9')
        else:
            self.assert_num_rows(2, -13, 'landuse', 'farmland', 'Name_rel9')

    def test_nested_outer_ways_area(self):
        self.assert_sum_area(16169, -7, 'landuse', 'farmland', 'Name_rel15')

    def test_nested_outer_ways_num(self):
        if '-G' in self.extra_params:
            self.assert_num_geoms(2, -7, 'landuse', 'farmland', 'Name_rel15')
        else:
            self.assert_num_rows(2, -7, 'landuse', 'farmland', 'Name_rel15')

    def test_no_copy_tags_from_outer(self):
        self.assert_area(18501, -24, 'natural', 'water')

    def test_outer_way_is_present(self):
        self.assert_area(24859, 83, 'landuse', 'farmland')

    def test_diff_move_point_inner_way(self):
        self.assert_area(13949 if self.update else 12895,
                         -1, 'landuse', 'residential', 'Name_rel')

    def test_diff_remove_relation(self):
        self.assert_count(0 if self.update else 1,
                         'planet_osm_polygon', where='osm_id = -25')

    def test_tags_on_inner_and_outer(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 113')

    def test_tags_on_inner_and_outer(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 118')

    def test_tags_on_outer_present(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 114')

    def test_tags_on_outer_absence_relation(self):
        self.assert_num_rows(0, -33, 'natural', 'water')

    def test_tags_on_relation_includes_relatoin(self):
        self.assert_sum_area(29155 if self.update else 68494,
                             -29, 'natural', 'water')

    def test_tags_relation_outer_1_absence(self):
        self.assert_count(0, 'planet_osm_polygon', where='osm_id = 109')

    def test_tags_relation_outer_2_absence(self):
        self.assert_count(0, 'planet_osm_polygon', where='osm_id = 104')

    def test_tags_relation_outer_1_presence(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 107')

    def test_tags_relation_outer_2_presence(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 102')

    def test_tags_relation_outer_2_presence_area(self):
        self.assert_area(12994, 102, 'natural', 'water')

    def test_tags_on_relation_and_way_relation_presence(self):
        self.assert_sum_area(10377, -39, 'landuse', 'forest')

    def test_tags_on_relation_and_way_way_presence(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 138')

    def test_different_tags_on_relation_and_way_relation_presence(self):
        self.assert_sum_area(12397, -40, 'landuse', 'forest')

    def test_different_tags_on_relation_and_way_way_presence(self):
        self.assert_count(1, 'planet_osm_polygon', where='osm_id = 140')

    def test_planet_osm_nodes(self):
        self.assert_count(1, 'pg_tables',
                          where="tablename = 'planet_osm_nodes'")


########################################################################
#
# Test runs
#
# Each class represents one import/update run. The class setup function
# in the runner trigger a single import for each class. Then the tests
# themselves check the state of the database after the import.

class TestPgsqlImportNonSlim(BaseImportRunner, unittest.TestCase,
                             PgsqlBaseTests, PgsqlMercGeomTests,
                             DroppedTableTests):
    extra_params = []

class TestPgsqlImportNonSlimLatLon(BaseImportRunner, unittest.TestCase,
                                   PgsqlBaseTests, DroppedTableTests):
    extra_params = ['-l']

class TestPgsqlImportSlimDrop(BaseImportRunner, unittest.TestCase,
                              PgsqlBaseTests, PgsqlMercGeomTests,
                              DroppedTableTests):
    extra_params = ['--slim', '--drop']

class TestPgsqlImportNonSlimLua(BaseImportRunner, unittest.TestCase,
                                PgsqlBaseTests, PgsqlMercGeomTests,
                                DroppedTableTests):
    extra_params = []
    use_lua_tagtransform = True

class TextPgsqlImportSlimHstoreDrop(BaseImportRunner, unittest.TestCase,
                                       PgsqlBaseTests, DroppedTableTests):
    extra_params = ['--slim', '--hstore', '--hstore-add-index', '--drop']

class TestPgsqlImportSlim(BaseImportRunner, unittest.TestCase,
                          PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim']

class TestPgsqlImportSlimParallel(BaseImportRunner, unittest.TestCase,
                                  PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--number-processes', '16']

class TestPgsqlImportSlimSmallCache(BaseImportRunner, unittest.TestCase,
                                    PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--number-processes', '8',
                    '-C1', '--cache-strategy=dense']

class TestPgsqlImportSlimNoCache(BaseImportRunner, unittest.TestCase,
                                 PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '-C0']

class TestPgsqlImportSlimHstoreMatchOnly(BaseImportRunner, unittest.TestCase,
                                         PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '-k', '--hstore-match-only']

class TestPgsqlImportSlimHstoreNameColumn(BaseImportRunner, unittest.TestCase,
                                          PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '-z', 'name:']

class TestPgsqlImportSlimHstore(BaseImportRunner, unittest.TestCase,
                                PgsqlBaseTests):
    extra_params = ['--slim', '-k']

class TestPgsqlImportSlimHstoreAll(BaseImportRunner, unittest.TestCase,
                                   PgsqlBaseTests, HstoreTagTests):
    extra_params = ['--slim', '-j']

class TestPgsqlImportSlimHstoreIndex(BaseImportRunner, unittest.TestCase,
                                     PgsqlBaseTests):
    extra_params = ['--slim', '--hstore', '--hstore-add-index']

class TestPgsqlImportSlimHstoreExtra(BaseImportRunner, unittest.TestCase,
                                     PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '-k', '-x', '--hstore-match-only']

class TestPgsqlImportSlimHstoreAllExtra(BaseImportRunner, unittest.TestCase,
                                        PgsqlBaseTests, ExtraTagTests):
    extra_params = ['--slim', '-j', '-x']

class TestPgsqlImportTablespaceMainData(BaseImportRunner, unittest.TestCase,
                                        PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--tablespace-main-data', 'tablespacetest']

class TestPgsqlImportTablespaceMainIndex(BaseImportRunner, unittest.TestCase,
                                         PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--tablespace-main-index', 'tablespacetest']

class TestPgsqlImportTablespaceSlimData(BaseImportRunner, unittest.TestCase,
                                        PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--tablespace-slim-data', 'tablespacetest']

class TestPgsqlImportTablespaceSlimIndex(BaseImportRunner, unittest.TestCase,
                                         PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim', '--tablespace-slim-index', 'tablespacetest']

class TestPgsqlImportSlimLua(BaseImportRunner, unittest.TestCase,
                             PgsqlBaseTests, PgsqlMercGeomTests):
    extra_params = ['--slim']
    use_lua_tagtransform = True


# Standard output updates

class TestPgsqlUpdate(BaseUpdateRunner, unittest.TestCase,
                      PgsqlBaseTests):
    extra_params = ['--slim']

class TestPgsqlUpdateParallel(BaseUpdateRunner, unittest.TestCase,
                              PgsqlBaseTests):
    extra_params = ['--slim', '--number-processes', '15']

class TestPgsqlUpdateSmallCache(BaseUpdateRunner, unittest.TestCase,
                                PgsqlBaseTests):
    extra_params = ['--slim', '--number-processes', '8',
                    '-C1', '--cache-strategy=dense']

class TestPgsqlUpdateNoCache(BaseUpdateRunner, unittest.TestCase,
                             PgsqlBaseTests):
    extra_params = ['--slim', '-C0']

class TestPgsqlUpdateHstoreMatchOnly(BaseUpdateRunner, unittest.TestCase,
                                     PgsqlBaseTests):
    extra_params = ['--slim', '-k', '--hstore-match-only']

class TestPgsqlUpdateHstoreNameColumn(BaseUpdateRunner, unittest.TestCase,
                                      PgsqlBaseTests):
    extra_params = ['--slim', '-z', 'name:']

class TestPgsqlUpdateHstore(BaseUpdateRunner, unittest.TestCase,
                            PgsqlBaseTests):
    extra_params = ['--slim', '-k']

class TestPgsqlUpdateHstoreAll(BaseUpdateRunner, unittest.TestCase,
                               PgsqlBaseTests, HstoreTagTests):
    extra_params = ['--slim', '-j']

class TestPgsqlUpdateHstoreIndex(BaseUpdateRunner, unittest.TestCase,
                                 PgsqlBaseTests):
    extra_params = ['--slim', '--hstore', '--hstore-add-index']

class TestPgsqlUpdateHstoreExtra(BaseUpdateRunner, unittest.TestCase,
                                 PgsqlBaseTests):
    extra_params = ['--slim', '-k', '-x', '--hstore-match-only']

class TestPgsqlUpdateHstoreAllExtra(BaseUpdateRunner, unittest.TestCase,
                                    PgsqlBaseTests):
    extra_params = ['--slim', '-j', '-x']

class TestPgsqlUpdateTablespaceMainData(BaseUpdateRunner, unittest.TestCase,
                                        PgsqlBaseTests):
    extra_params = ['--slim', '--tablespace-main-data', 'tablespacetest']

class TestPgsqlUpdateTablespaceMainIndex(BaseUpdateRunner, unittest.TestCase,
                                         PgsqlBaseTests):
    extra_params = ['--slim', '--tablespace-main-index', 'tablespacetest']

class TestPgsqlUpdateTablespaceSlimData(BaseUpdateRunner, unittest.TestCase,
                                        PgsqlBaseTests):
    extra_params = ['--slim', '--tablespace-slim-data', 'tablespacetest']

class TestPgsqlUpdateTablespaceSlimIndex(BaseUpdateRunner, unittest.TestCase,
                                         PgsqlBaseTests):
    extra_params = ['--slim', '--tablespace-slim-index', 'tablespacetest']

class TestPgsqlUpdateLua(BaseUpdateRunner, unittest.TestCase,
                         PgsqlBaseTests):
    extra_params = ['--slim']
    use_lua_tagtransform = True

# Gazetteer output

class TestGazetteerImport(BaseImportRunner, unittest.TestCase, GazetteerTests):
    extra_params = ['--slim']
    use_gazetteer = True

class TestGazetteerUpdate(BaseUpdateRunner, unittest.TestCase, GazetteerTests):
    extra_params = ['--slim']
    use_gazetteer = True

# Multipolygon import tests

class TestMPImportSlim(MultipolygonImportRunner, unittest.TestCase,
                       MultipolygonTests):
    extra_params = ['--slim']

class TestMPImportSlimMultiGeom(MultipolygonImportRunner, unittest.TestCase,
                                MultipolygonTests):
    extra_params = ['--slim', '-G']

class TestMPImportSlimHstore(MultipolygonImportRunner, unittest.TestCase,
                             MultipolygonTests):
    extra_params = ['--slim', '-k']

class TestMPImportSlimHstoreMatch(MultipolygonImportRunner, unittest.TestCase,
                                  MultipolygonTests):
    extra_params = ['--slim', '-k', '--hstore-match-only']

class TestMPImportSlimHstoreExtra(MultipolygonImportRunner, unittest.TestCase,
                                  MultipolygonTests):
    extra_params = ['--slim', '-x', '-k', '--hstore-match-only']

class TestMPImportSlimLua(MultipolygonImportRunner, unittest.TestCase,
                          MultipolygonTests):
    extra_params = ['--slim']
    use_lua_tagtransform = True

class TestMPImportSlimLuaHstore(MultipolygonImportRunner, unittest.TestCase,
                                MultipolygonTests):
    extra_params = ['--slim', '-k']
    use_lua_tagtransform = True

# Multipolygon update tests

class TestMPUpdateSlim(MultipolygonUpdateRunner, unittest.TestCase,
                       MultipolygonTests):
    extra_params = ['--slim']

class TestMPUpdateSlimMultiGeom(MultipolygonUpdateRunner, unittest.TestCase,
                                MultipolygonTests):
    extra_params = ['--slim', '-G']

class TestMPUpdateSlimHstore(MultipolygonUpdateRunner, unittest.TestCase,
                             MultipolygonTests):
    extra_params = ['--slim', '-k']

class TestMPUpdateSlimHstoreMatch(MultipolygonUpdateRunner, unittest.TestCase,
                                  MultipolygonTests):
    extra_params = ['--slim', '-k', '--hstore-match-only']

class TestMPUpdateSlimHstoreExtra(MultipolygonUpdateRunner, unittest.TestCase,
                                  MultipolygonTests):
    extra_params = ['--slim', '-x', '-k', '--hstore-match-only']

class TestMPUpdateSlimLua(MultipolygonUpdateRunner, unittest.TestCase,
                          MultipolygonTests):
    extra_params = ['--slim']
    use_lua_tagtransform = True

class TestMPUpdateSlimLuaHstore(MultipolygonUpdateRunner, unittest.TestCase,
                                MultipolygonTests):
    extra_params = ['--slim', '-k']
    use_lua_tagtransform = True

class TestMPUpdateSlimWithFlatNodes(MultipolygonUpdateRunner, unittest.TestCase,
                                    MultipolygonTests):
    extra_params = ['--slim', '-F', 'flat.nodes']

    def test_planet_osm_nodes(self):
        self.assert_count(0, 'pg_tables',
                          where="tablename = 'planet_osm_nodes'")

class TestMPUpdateWithForwardDependencies(DependencyUpdateRunner, unittest.TestCase):
    extra_params = ['--latlong', '--slim', '--with-forward-dependencies=true']

    def test_count(self):
        self.assert_count(1, 'planet_osm_point')
        self.assert_count(1, 'planet_osm_line')
        self.assert_count(0, 'planet_osm_line', 'abs(ST_X(ST_StartPoint(way)) - 3.0) < 0.0001')
        self.assert_count(1, 'planet_osm_line', 'abs(ST_X(ST_StartPoint(way)) - 3.1) < 0.0001')
        self.assert_count(1, 'planet_osm_roads')
        self.assert_count(1, 'planet_osm_polygon')

class TestMPUpdateWithoutForwardDependencies(DependencyUpdateRunner, unittest.TestCase):
    extra_params = ['--latlong', '--slim', '--with-forward-dependencies=false']

    def test_count(self):
        self.assert_count(1, 'planet_osm_point')
        self.assert_count(1, 'planet_osm_line')
        self.assert_count(1, 'planet_osm_line', 'abs(ST_X(ST_StartPoint(way)) - 3.0) < 0.0001')
        self.assert_count(0, 'planet_osm_line', 'abs(ST_X(ST_StartPoint(way)) - 3.1) < 0.0001')
        self.assert_count(1, 'planet_osm_roads')
        self.assert_count(2, 'planet_osm_polygon')

# Database access tests

class TestDBAccessNorm(BaseUpdateRunner, unittest.TestCase,
                       PgsqlBaseTests):
    extra_params = ['--slim', '-d', CONFIG['test_database']]

class TestDBAccessNormLong(BaseUpdateRunner, unittest.TestCase,
                       PgsqlBaseTests):
    extra_params = ['--slim', '--database', CONFIG['test_database']]

class TestDBAccessConninfo(BaseUpdateRunner, unittest.TestCase,
                           PgsqlBaseTests):
    extra_params = ['--slim', '-d', 'dbname=' + CONFIG['test_database']]

class TestDBAccessConninfoLong(BaseUpdateRunner, unittest.TestCase,
                               PgsqlBaseTests):
    extra_params = ['--slim', '--database', 'dbname=' + CONFIG['test_database']]

class TestDBAccessURIPostgresql(BaseUpdateRunner, unittest.TestCase,
                                PgsqlBaseTests):
    extra_params = ['--slim', '-d', 'postgresql:///' + CONFIG['test_database']]

class TestDBAccessURIPostgres(BaseUpdateRunner, unittest.TestCase,
                              PgsqlBaseTests):
    extra_params = ['--slim', '-d', 'postgres:///' + CONFIG['test_database']]

# Schema tests

class TestDBOutputSchema(BaseUpdateRunnerWithOutputSchema, unittest.TestCase,
                         PgsqlBaseTests):
    extra_params = ['--slim', '--output-pgsql-schema=osm']

