# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2006-2025 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps that query the database.
"""
import math
import re
from typing import Iterable

try:
    from psycopg2 import sql
except ImportError:
    from psycopg import sql


@given("the database schema (?P<schema>.+)")
def create_db_schema(context, schema):
    with context.db.cursor() as cur:
        cur.execute("CREATE SCHEMA " + schema)


@when("deleting table (?P<table>.+)")
def delete_table(context, table):
    with context.db.cursor() as cur:
        cur.execute("DROP TABLE " + table)


@then(r"table (?P<table>.+) has (?P<row_num>\d+) rows?(?P<has_where> with condition)?")
def db_table_row_count(context, table, row_num, has_where):
    assert table_exists(context.db, table)

    query = sql.SQL("SELECT count(*) FROM {}").format(sql.Identifier(*table.split('.', 2)))

    if has_where:
        query = sql.SQL("{} WHERE {}").format(query, sql.SQL(context.text))

    actual = scalar(context.db, query)

    assert actual == int(row_num),\
           f"Table {table}: expected {row_num} rows, got {actual}"


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
        cur.execute(sql.SQL("SELECT {} FROM {}")
                       .format(rows, sql.Identifier(*table.split('.', 2))))

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
    if num == 1:
        return True

    num = scalar(conn, """SELECT count(*) FROM pg_views
                          WHERE viewname = %s AND schemaname = %s""",
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
            elif props == 'fullmatch':
                self.data.append(DBValueRegex(value))
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
        self.precision = float(props) if props else 0.0001
        self.orig_value = value
        self.set_coordinates(value)
        self.factory = factory

    def set_coordinates(self, value):
        if value.startswith('GEOMETRYCOLLECTION('):
            geoms = []
            remain = value[19:-1]
            while remain:
                _, value, remain = self._parse_simple_wkt(remain)
                remain = remain[1:] # delete comma
                geoms.append(value)
            self.geom_type = 'GEOMETRYCOLLECTION'
            self.value = geoms
        else:
            self.geom_type, self.value, remain = self._parse_simple_wkt(value)
            if remain:
                raise RuntimeError('trailing content for geometry: ' + value)

    def _parse_simple_wkt(self, value):
        m = re.fullmatch(r'(MULTI)?(POINT|LINESTRING|POLYGON)\(([^A-Z]*)\)(.*)', value)
        if not m:
            raise RuntimeError(f'Unparsable WKT: {value}')
        geom_type = (m[1] or '') + m[2]
        if m[1] == 'MULTI':
            splitup = m[3][1:-1].split('),(')
            if m[2] == 'POINT':
                value = [self._parse_wkt_coord(c) for c in splitup]
            elif m[2] == 'LINESTRING':
                value = [self._parse_wkt_line(c) for c in splitup]
            elif m[2] == 'POLYGON':
                value = [[self._parse_wkt_line(ln) for ln in poly[1:-1].split('),(')]
                         for poly in splitup]
        else:
            if m[2] == 'POINT':
                value = self._parse_wkt_coord(m[3])
            elif m[2] == 'LINESTRING':
                value = self._parse_wkt_line(m[3])
            elif m[2] == 'POLYGON':
                value = [self._parse_wkt_line(ln) for ln in m[3][1:-1].split('),(')]

        return geom_type, value, m[4]

    def _parse_wkt_coord(self, coord):
        return tuple(DBValueFloat(float(f.strip()), self.precision) for f in coord.split())

    def _parse_wkt_line(self, coords):
        return [self._parse_wkt_coord(pt) for pt in coords.split(',')]

    def __eq__(self, other):
        if other.startswith('[') and other.endswith(']'):
            gtype = 'MULTI'
            toparse = other[1:-1].split(';')
        elif other.startswith('{') and other.endswith('}'):
            gtype = 'GEOMETRYCOLLECTION'
            toparse = other[1:-1].split(';')
        else:
            gtype = None
            toparse = [other]

        geoms = []
        for sub in toparse:
            sub = sub.strip()
            if sub.find(',') < 0:
                geoms.append(self._parse_input_coord(sub))
                if gtype is None:
                    gtype = 'POINT'
                elif gtype.startswith('MULTI'):
                    if gtype == 'MULTI':
                        gtype = 'MULTIPOINT'
                    elif gtype != 'MULTIPOINT':
                        raise RuntimeError('MULTI* geometry with different geometry types is not supported.')
            elif sub.find('(') < 0:
                geoms.append(self._parse_input_line(sub))
                if gtype is None:
                    gtype = 'LINESTRING'
                elif gtype.startswith('MULTI'):
                    if gtype == 'MULTI':
                        gtype = 'MULTILINESTRING'
                    elif gtype != 'MULTILINESTRING':
                        raise RuntimeError('MULTI* geometry with different geometry types is not supported.')
            else:
                geoms.append([self._parse_input_line(ln) for ln in sub.strip()[1:-1].split('),(')])
                if gtype is None:
                    gtype = 'POLYGON'
                elif gtype.startswith('MULTI'):
                    if gtype == 'MULTI':
                        gtype = 'MULTIPOLYGON'
                    elif gtype != 'MULTIPOLYGON':
                        raise RuntimeError('MULTI* geometry with different geometry types is not supported.')

        if not gtype.startswith('MULTI') and gtype != 'GEOMETRYCOLLECTION':
            geoms = geoms[0]

        return gtype == self.geom_type and self.value == geoms

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


class DBValueRegex:

    def __init__(self, value):
        self.value = str(value)

    def __eq__(self, other):
        return re.fullmatch(str(other), self.value) is not None

    def __repr__(self):
        return repr(self.value)
