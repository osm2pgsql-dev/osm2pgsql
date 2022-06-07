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
import re
from typing import Iterable

from psycopg2 import sql

@given("the database schema (?P<schema>.+)")
def create_db_schema(context, schema):
    with context.db.cursor() as cur:
        cur.execute("CREATE SCHEMA " + schema)

@then("table (?P<table>.+) has (?P<row_num>\d+) rows?(?P<has_where> with condition)?")
def db_table_row_count(context, table, row_num, has_where):
    assert table_exists(context.db, table)

    if '.' in table:
        schema, tablename = table.split('.', 2)
        query = sql.SQL("SELECT count(*) FROM {}.{}")\
                   .format(sql.Identifier(schema), sql.Identifier(tablename))
    else:
        query = sql.SQL("SELECT count(*) FROM {}").format(sql.Identifier(table))

    if has_where:
        query = sql.SQL("{} WHERE {}").format(query, sql.SQL(context.text))

    actual = scalar(context.db, query)

    assert actual == int(row_num),\
           f"Table {table}: expected {row_num} rows, got {actual}"


@then("the sum of '(?P<formula>.+)' in table (?P<table>.+) is (?P<result>\d+)(?P<has_where> with condition)?")
def db_table_sum_up(context, table, formula, result, has_where):
    assert table_exists(context.db, table)

    query = sql.SQL("SELECT round(sum({})) FROM {}")\
               .format(sql.SQL(formula), sql.Identifier(table))

    if has_where:
        query = sql.SQL("{} WHERE {}").format(query, sql.SQL(context.text))


    actual = scalar(context.db, query)

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


@then("table (?P<table>.+) contains(?P<exact> exactly)?")
def db_check_table_content(context, table, exact):
    assert table_exists(context.db, table)

    rows = sql.SQL(', '.join(h.rsplit('@')[0] for h in context.table.headings))

    with context.db.cursor() as cur:
        cur.execute(sql.SQL("SELECT {} FROM {}").format(rows, sql.Identifier(table)))

        actuals = list(DBRow(r, context.table.headings, context.geometry_factory) for r in cur)

    linenr = 1
    for row in context.table.rows:
        try:
            actuals.remove(row)
        except ValueError:
            assert False,\
                   f"{linenr}. entry not found in table. Full content:\n{actuals}"
        linenr += 1

    assert not exact or not actuals,\
           f"Unexpected lines in row:\n{actuals}"

@then("(?P<query>SELECT .*)")
def db_check_sql_statement(context, query):
    with context.db.cursor() as cur:
        cur.execute(query)

        actuals = list(DBRow(r, context.table.headings, context.geometry_factory) for r in cur)

    linenr = 1
    for row in context.table.rows:
        assert any(r == row for r in actuals),\
               f"{linenr}. entry not found in table. Full content:\n{actuals}"
        linenr += 1


### Helper functions and classes

def scalar(conn, sql, args=None):
    with conn.cursor() as cur:
        cur.execute(sql, args)

        assert cur.rowcount == 1
        return cur.fetchone()[0]

def table_exists(conn, table):
    if '.' in table:
        schema, tablename = table.split('.', 2)
    else:
        schema = 'public'
        tablename = table

    num = scalar(conn, """SELECT count(*) FROM pg_tables
                          WHERE tablename = %s AND schemaname = %s""",
                (tablename, schema))
    return num == 1


class DBRow:

    def __init__(self, row, headings, factory):
        self.data = []
        for value, head in zip(row, headings):
            if '@' in head:
                _, props = head.rsplit('@', 2)
            else:
                props = None

            if isinstance(value, float):
                self.data.append(DBValueFloat(value, props))
            elif value is None:
                self.data.append(None)
            elif head.lower().startswith('st_astext('):
                self.data.append(DBValueGeometry(value, props, factory))
            else:
                self.data.append(str(value))

    def __eq__(self, other):
        if not isinstance(other, Iterable):
            return False

        return all((a is None) if b == 'NULL' else (a == b)
                   for a, b in zip(self.data, other))

    def __repr__(self):
        return '\n[' + ', '.join(str(s) for s in self.data) + ']'


class DBValueGeometry:

    def __init__(self, value, props, factory):
        self.precision = float(props) if props else 0.01
        self.orig_value = value
        self.set_coordinates(value)
        self.factory = factory

    def set_coordinates(self, value):
        m = re.fullmatch(r'(POINT|LINESTRING|POLYGON)\((.*)\)', value)
        if not m:
            raise RuntimeError(f'Unparsable WKT: {value}')
        if m[1] == 'POINT':
            self.value = self._parse_wkt_coord(m[2])
        elif m[1] == 'LINESTRING':
            self.value = self._parse_wkt_line(m[2])
        elif m[1] == 'POLYGON':
            self.value = [self._parse_wkt_line(ln) for ln in m[2][1:-1].split('),(')]

    def _parse_wkt_coord(self, coord):
        return tuple(DBValueFloat(float(f.strip()), self.precision) for f in coord.split())

    def _parse_wkt_line(self, coords):
        return [self._parse_wkt_coord(pt) for pt in coords.split(',')]

    def __eq__(self, other):
        if other.find(',') < 0:
            geom = self._parse_input_coord(other)
        elif other.find('(') < 0:
            geom = self._parse_input_line(other)
        else:
            geom = [self._parse_input_line(ln) for ln in other.strip()[1:-1].split('),(')]

        return self.value == geom

    def _parse_input_coord(self, other):
        coords = other.split(' ')
        if len(coords) == 1:
            return self.factory.grid_node(int(coords[0]))
        if len(coords) == 2:
            return tuple(float(c.strip()) for c in coords)

        raise RuntimeError(f'Bad coordinate: {other}')

    def _parse_input_line(self, other):
        return [self._parse_input_coord(pt.strip()) for pt in other.split(',')]

    def __repr__(self):
        return self.orig_value


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
