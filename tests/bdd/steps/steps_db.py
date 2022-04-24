# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps that query the database.
"""
import math
from typing import Iterable

from psycopg2 import sql

@then("table (?P<table>.+) has (?P<row_num>\d+) rows(?P<has_where> with condition)?")
def db_table_row_count(context, table, row_num, has_where):
    assert table_exists(context.db, table)

    query = sql.SQL("SELECT count(*) FROM {}").format(sql.Identifier(table))

    if has_where:
        query = sql.SQL("{} WHERE {}").format(query, sql.SQL(context.text))

    actual = scalar(context.db, query)

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


@then("there (?:is|are) (?P<exists>no )?tables? (?P<tables>.+)")
def db_table_existance(context, exists, tables):
    for table in tables.split(','):
        table = table.strip()
        if exists == 'no ':
            assert not table_exists(context.db, table), f"Table '{table}' unexpectedly found"
        else:
            assert table_exists(context.db, table), f"Table '{table}' not found"


@then("table (?P<table>.+) contains")
def db_check_table_content(context, table):
    assert table_exists(context.db, table)

    rows = sql.SQL(', '.join(h.rsplit('@')[0] for h in context.table.headings))

    with context.db.cursor() as cur:
        cur.execute(sql.SQL("SELECT {} FROM {}").format(rows, sql.Identifier(table)))

        actuals = list(DBRow(r, context.table.headings) for r in cur)

    linenr = 1
    for row in context.table.rows:
        assert any(r == row for r in actuals), f"{linenr}. entry not found in table. Full content:\n{actuals}"
        linenr += 1

### Helper functions and classes

def scalar(conn, sql, args=None):
    with conn.cursor() as cur:
        cur.execute(sql, args)

        assert cur.rowcount == 1
        return cur.fetchone()[0]

def table_exists(conn, table):
    num = scalar(conn, "SELECT count(*) FROM pg_tables WHERE tablename = %s",
                (table, ))
    return num == 1


class DBRow:

    def __init__(self, row, headings):
        self.data = []
        for value, head in zip(row, headings):
            if '@' in head:
                _, props = head.rsplit('@', 2)
            else:
                props = None

            if isinstance(value, float):
                self.data.append(DBValueFloat(value, props))
            else:
                self.data.append(str(value))

    def __eq__(self, other):
        if not isinstance(other, Iterable):
            return False

        return all(a == b for a, b in zip(self.data, other))

    def __repr__(self):
        return '\n[' + ', '.join(str(s) for s in self.data) + ']'


class DBValueFloat:

    def __init__(self, value, props):
        self.precision = float(props) if props else 0.0001
        self.value = value

    def __eq__(self, other):
        try:
            fother = float(other)
        except:
            return False

        return math.isclose(self.value, fother, rel_tol=self.precision)

    def __repr__(self):
        return repr(self.value)
