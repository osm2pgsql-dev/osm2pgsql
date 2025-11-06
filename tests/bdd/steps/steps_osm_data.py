# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2006-2025 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for creating the OSM import file.
"""

def add_opl_lines(context, lines):
    for line in lines:
        if (oplobj := line.strip()):
            assert oplobj[0] in ('n', 'w', 'r')
            oplobj = context.geometry_factory.complete_opl(oplobj)
            data = context.import_data[oplobj[0]]
            objid = oplobj.split(' ', 1)[0] + ' '
            for i, existing in enumerate(data):
                if existing.startswith(objid):
                    data[i] = oplobj
                    break
            else:
                data.append(oplobj)


@given("the input file '(?P<osm_file>.+)'")
def osm_set_import_file(context, osm_file):
    """ Use an OSM file from the test directory for the import.
    """
    context.import_file = context.test_data_dir / osm_file


@given("the (?P<step>[0-9.]+ )?grid(?: with origin (?P<origin_x>[0-9.-]+) (?P<origin_y>[0-9.-]+))?")
def osm_define_node_grid(context, step, origin_x, origin_y):
    step = float(step.strip()) if step else 0.1
    x = float(origin_x) if origin_x else 20.0
    y = float(origin_y) if origin_y else 20.0

    assert x >= -180.0 and x <= 180.0
    assert y >= -90.0 and y <= 90.0

    context.geometry_factory.set_grid([context.table.headings] + [list(h) for h in context.table],
                                      step, x, y)
    add_opl_lines(context, context.geometry_factory.as_opl_lines())


@given("the (?P<formatted>python-formatted )?OSM data")
def osm_define_data(context, formatted):
    context.import_file = None
    data = context.text
    if formatted:
        data = eval('f"""' + data + '"""')

    add_opl_lines(context, data.split('\n'))
