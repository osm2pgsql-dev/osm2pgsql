# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for creating the OSM import file.
"""


@given("the input file '(?P<osm_file>.+)'")
def set_import_file(context, osm_file):
    context.import_file = context.test_data_dir / osm_file
