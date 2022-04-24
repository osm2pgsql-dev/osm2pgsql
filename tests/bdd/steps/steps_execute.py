# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for executing osm2pgsql.
"""
import subprocess

@when("running osm2pgsql (?P<output>\w+)")
def run_osm2pgsql(context, output):
    assert output in ('flex', 'pgsql', 'gazetteer', 'none')
    assert context.import_file is not None

    cmdline = [str(context.config.userdata['BINARY'])]
    cmdline.extend(('-d', context.config.userdata['TEST_DB']))

    if output == 'pgsql':
        if '-S' not in context.osm2pgsql_params:
            cmdline.extend(('-S', str(context.default_data_dir / 'default.style')))

    cmdline.extend(context.osm2pgsql_params)
    cmdline.append(context.import_file)

    proc = subprocess.Popen(cmdline,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    outdata = proc.communicate()

    outdata = [d.decode('utf-8').replace('\\n', '\n') for d in outdata]

    assert proc.returncode == 0,\
           f"osm2psql failed with error code {proc.returncode}.\n"\
           f"Output:\n{outdata[0]}\n{outdata[1]}\n"
