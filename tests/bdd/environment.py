# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2006-2025 by the osm2pgsql developer community.
# For a full list of authors see the git log.
from contextlib import closing
from pathlib import Path
import subprocess
import tempfile
import importlib.util
import io
from importlib.machinery import SourceFileLoader

from behave import *
try:
    import psycopg2 as psycopg
    from psycopg2 import sql
except ImportError:
    import psycopg
    from psycopg import sql


from steps.geometry_factory import GeometryFactory
from steps.replication_server_mock import ReplicationServerMock

TEST_BASE_DIR = (Path(__file__) / '..' / '..').resolve()

# The following parameters can be changed on the command line using
# the -D parameter. Example:
#
#    behave -DBINARY=/tmp/my-builddir/osm2pgsql -DKEEP_TEST_DB
USER_CONFIG = {
    'BINARY': (TEST_BASE_DIR / '..' / 'build' / 'osm2pgsql').resolve(),
    'REPLICATION_SCRIPT': (TEST_BASE_DIR / '..' / 'scripts' / 'osm2pgsql-replication').resolve(),
    'TEST_DATA_DIR': TEST_BASE_DIR / 'data',
    'SRC_DIR': (TEST_BASE_DIR / '..').resolve(),
    'KEEP_TEST_DB': False,
    'TEST_DB': 'osm2pgsql-test',
    'HAVE_TABLESPACE': True,
    'HAVE_PROJ': True
}

use_step_matcher('re')

def _connect_db(context, dbname):
    """ Connect to the given database and return the connection
        object as a context manager that automatically closes.
        Note that the connection does not commit automatically.
    """
    if psycopg.__version__.startswith('2'):
        conn = psycopg.connect(dbname=dbname)
        conn.autocommit = True
        return closing(conn)

    return psycopg.connect(dbname=dbname, autocommit=True)


def _drop_db(context, dbname, recreate_immediately=False):
    """ Drop the database with the given name if it exists.
    """
    with _connect_db(context, 'postgres') as conn:
        with conn.cursor() as cur:
            db = sql.Identifier(dbname)
            cur.execute(sql.SQL('DROP DATABASE IF EXISTS {}').format(db))
            if recreate_immediately:
                cur.execute(sql.SQL('CREATE DATABASE {}').format(db))


def before_all(context):
    # logging setup
    context.config.setup_logging()
    # set up -D options
    for k,v in USER_CONFIG.items():
        context.config.userdata.setdefault(k, v)

    if context.config.userdata['HAVE_TABLESPACE']:
        with _connect_db(context, 'postgres') as conn:
            with conn.cursor() as cur:
                cur.execute("""SELECT spcname FROM pg_tablespace
                               WHERE spcname = 'tablespacetest'""")
                context.config.userdata['HAVE_TABLESPACE'] = cur.rowcount > 0
                cur.execute("""SELECT setting FROM pg_settings
                               WHERE name = 'server_version_num'""")
                context.config.userdata['PG_VERSION'] = int(cur.fetchone()[0])

    # Get the osm2pgsql configuration
    proc = subprocess.Popen([str(context.config.userdata['BINARY']), '--version'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    _, serr = proc.communicate()
    ver_info = serr.decode('utf-8')
    if proc.returncode != 0:
        raise RuntimeError('Cannot run osm2pgsql')

    if context.config.userdata['HAVE_PROJ']:
        context.config.userdata['HAVE_PROJ'] = 'Proj [disabled]' not in ver_info

    context.test_data_dir = Path(context.config.userdata['TEST_DATA_DIR']).resolve()
    context.default_data_dir = Path(context.config.userdata['SRC_DIR']).resolve()

    # Set up replication script.
    replicationfile = str(Path(context.config.userdata['REPLICATION_SCRIPT']).resolve())
    spec = importlib.util.spec_from_loader('osm2pgsql_replication',
                                           SourceFileLoader('osm2pgsql_replication',
                                                            replicationfile))
    assert spec, f"File not found: {replicationfile}"
    context.osm2pgsql_replication = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(context.osm2pgsql_replication)


def before_scenario(context, scenario):
    """ Set up a fresh, empty test database.
    """
    if 'config.have_proj' in scenario.tags and not context.config.userdata['HAVE_PROJ']:
        scenario.skip("Generic proj library not configured.")

    context.db = use_fixture(test_db, context)
    context.import_file = None
    context.import_data = {'n': [], 'w': [], 'r': []}
    context.osm2pgsql_params = []
    context.workdir = use_fixture(working_directory, context)
    context.geometry_factory = GeometryFactory()
    context.osm2pgsql_replication.ReplicationServer = ReplicationServerMock()
    context.urlrequest_responses = {}

    def _mock_urlopen(request):
        if not request.full_url in context.urlrequest_responses:
            raise urllib.error.URLError('Unknown URL')

        return closing(io.BytesIO(context.urlrequest_responses[request.full_url].encode('utf-8')))

    context.osm2pgsql_replication.urlrequest.urlopen = _mock_urlopen


@fixture
def test_db(context, **kwargs):
    dbname = context.config.userdata['TEST_DB']
    _drop_db(context, dbname, recreate_immediately=True)

    with _connect_db(context, dbname) as conn:

        with conn.cursor() as cur:
            cur.execute('CREATE EXTENSION postgis')
            cur.execute('CREATE EXTENSION hstore')

        yield conn

    if not context.config.userdata['KEEP_TEST_DB']:
        _drop_db(context, dbname)


@fixture
def working_directory(context, **kwargs):
    with tempfile.TemporaryDirectory() as tmpdir:
        yield Path(tmpdir)


def before_tag(context, tag):
    if tag == 'needs-pg-index-includes':
        if context.config.userdata['PG_VERSION'] < 110000:
            context.scenario.skip("No index includes in PostgreSQL < 11")

