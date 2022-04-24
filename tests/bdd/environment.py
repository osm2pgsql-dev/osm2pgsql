# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
from contextlib import closing
from pathlib import Path
import tempfile

from behave import *
import psycopg2
from psycopg2 import sql

from steps.geometry_factory import GeometryFactory

TEST_BASE_DIR = (Path(__file__) / '..' / '..').resolve()

# The following parameters can be changed on the command line using
# the -D parameter. Example:
#
#    behave -DBINARY=/tmp/my-builddir/osm2pgsql -DKEEP_TEST_DB
USER_CONFIG = {
    'BINARY': (TEST_BASE_DIR / '..' / 'build' / 'osm2pgsql').resolve(),
    'TEST_DATA_DIR': TEST_BASE_DIR / 'data',
    'SRC_DIR': (TEST_BASE_DIR / '..').resolve(),
    'KEEP_TEST_DB': False,
    'TEST_DB': 'osm2pgsql-test',
    'HAVE_TABLESPACE': True
}

use_step_matcher('re')

def _connect_db(context, dbname):
    """ Connect to the given database and return the conntection
        object as a context manager that automatically closes.
        Note that the connection does not commit automatically.
    """
    return closing(psycopg2.connect(dbname=dbname))


def _drop_db(context, dbname, recreate_immediately=False):
    """ Drop the database with the given name if it exists.
    """
    with _connect_db(context, 'postgres') as conn:
        conn.set_isolation_level(0)
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

    context.geometry_factory = GeometryFactory()
    context.test_data_dir = Path(context.config.userdata['TEST_DATA_DIR']).resolve()
    context.default_data_dir = Path(context.config.userdata['SRC_DIR']).resolve()


def before_scenario(context, scenario):
    """ Set up a fresh, empty test database.
    """
    _drop_db(context, context.config.userdata['TEST_DB'], recreate_immediately=True)

    context.db = use_fixture(test_db, context)
    context.import_file = None
    context.osm2pgsql_params = []
    context.workdir = use_fixture(working_directory, context)


@fixture
def test_db(context, **kwargs):
    dbname = context.config.userdata['TEST_DB']
    _drop_db(context, dbname, recreate_immediately=True)

    with _connect_db(context, dbname) as conn:
        conn.autocommit = True

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
