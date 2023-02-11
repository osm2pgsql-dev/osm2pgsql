# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2023 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for creating the OSM import file.
"""


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


@given("the (?P<formatted>python-formatted )?OSM data")
def osm_define_data(context, formatted):
    context.import_file = None
    data = context.text
    if formatted:
        data = eval('f"""' + data + '"""')

    for line in data.split('\n'):
        line = line.strip()
        if line:
            assert line[0] in ('n', 'w', 'r')
            context.import_data[line[0]].append(line)
