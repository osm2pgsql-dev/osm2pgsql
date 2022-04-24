# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps that query the database.
"""
from psycopg2 import sql

def scalar(conn, sql, args=None):
    with conn.cursor() as cur:
        cur.execute(sql, args)

        assert cur.rowcount == 1
        return cur.fetchone()[0]

def table_exists(conn, table):
    num = scalar(conn, "SELECT count(*) FROM pg_tables WHERE tablename = %s",
                (table, ))
    return num == 1


@then("table (?P<table>.+) has (?P<row_num>\d+) rows")
def db_table_row_count(context, table, row_num):
    assert table_exists(context.db, table)

    actual = scalar(context.db,
                    sql.SQL("SELECT count(*) FROM {}").format(sql.Identifier(table)))

    assert actual == int(row_num),\
           f"Table {table}: expected {row_num} rows, got {actual}"


@then("the sum of '(?P<formula>.+)' in table (?P<table>.+) is (?P<result>\d+)")
def db_table_sum_up(context, table, formula, result):
    assert table_exists(context.db, table)

    actual = scalar(context.db,
                    sql.SQL("SELECT round(sum({})) FROM {}")
                       .format(sql.SQL(formula), sql.Identifier(table)))

    assert actual == int(result),\
           f"Table {table}: expected sum {result}, got {actual}"


@then("there (?:is|are) (?P<exists>no )tables? (?P<tables>.+)")
def db_table_existance(context, exists, tables):
    for table in tables.split(','):
        table = table.strip()
        if exists == 'no ':
            assert not table_exists(context.db, table), f"Table '{table}' unexpectedly found"
        else:
            assert table_exists(context.db, table), f"Table '{table}' not found"
