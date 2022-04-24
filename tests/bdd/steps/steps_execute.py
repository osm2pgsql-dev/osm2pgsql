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

@given("the (?P<use_default>default )?lua tagtransform")
def setup_lua_tagtransform(context, use_default):
    assert use_default, "inline tag transform not implemented"

    if use_default:
        context.osm2pgsql_params.extend(('--tag-transform-script',
                                         str(context.default_data_dir / 'style.lua')))

@when("running osm2pgsql (?P<output>\w+)(?: with parameters)?")
def run_osm2pgsql(context, output):
    assert output in ('flex', 'pgsql', 'gazetteer', 'none')
    assert context.import_file is not None

    cmdline = [str(context.config.userdata['BINARY'])]
    cmdline.extend(('-d', context.config.userdata['TEST_DB']))
    cmdline.extend(context.osm2pgsql_params)

    if context.table:
        cmdline.extend(f for f in context.table.headings if f)
        for row in context.table:
            cmdline.extend(f for f in row if f)

    if 'tablespacetest' in cmdline and not context.config.userdata['HAVE_TABLESPACE']:
       context.scenario.skip('tablespace tablespacetest not available')
       return

    if output == 'pgsql':
        if '-S' not in cmdline:
            cmdline.extend(('-S', str(context.default_data_dir / 'default.style')))

    cmdline.append(context.import_file)

    proc = subprocess.Popen(cmdline,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    outdata = proc.communicate()

    outdata = [d.decode('utf-8').replace('\\n', '\n') for d in outdata]

    assert proc.returncode == 0,\
           f"osm2psql failed with error code {proc.returncode}.\n"\
           f"Output:\n{outdata[0]}\n{outdata[1]}\n"
