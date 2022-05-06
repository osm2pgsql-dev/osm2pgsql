# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for executing osm2pgsql.
"""
from pathlib import Path
import subprocess

def get_import_file(context):
    if context.import_file is not None:
        return str(context.import_file)

    context.geometry_factory.complete_node_list(context.import_data['n'])

    # sort by OSM id
    for obj in context.import_data.values():
        obj.sort(key=lambda l: int(l.split(' ')[0][1:]))

    data_file = context.workdir / "inline_import_data.opl"
    with data_file.open('w') as fd:
        for typ in ('n', 'w', 'r'):
            for line in context.import_data[typ]:
                fd.write(line)
                fd.write('\n')
            context.import_data[typ].clear()

    return str(data_file)


def run_osm2pgsql(context, output):
    assert output in ('flex', 'pgsql', 'gazetteer', 'none')

    cmdline = [str(Path(context.config.userdata['BINARY']).resolve())]
    cmdline.extend(('-O', output))
    cmdline.extend(context.osm2pgsql_params)

    if context.table:
        cmdline.extend(f for f in context.table.headings if f)
        for row in context.table:
            cmdline.extend(f.format(**context.config.userdata) for f in row if f)

    if '-d' not in cmdline and '--database' not in cmdline:
        cmdline.extend(('-d', context.config.userdata['TEST_DB']))

    if 'tablespacetest' in cmdline and not context.config.userdata['HAVE_TABLESPACE']:
       context.scenario.skip('tablespace tablespacetest not available')
       return

    if output == 'pgsql':
        if '-S' not in cmdline:
            cmdline.extend(('-S', str(context.default_data_dir / 'default.style')))

    cmdline.append(get_import_file(context))

    proc = subprocess.Popen(cmdline, cwd=str(context.workdir),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    outdata = proc.communicate()

    context.osm2psql_outdata = [d.decode('utf-8').replace('\\n', '\n') for d in outdata]

    return proc.returncode

@given("no lua tagtransform")
def do_not_setup_tagtransform(context):
    pass

@given("the default lua tagtransform")
def setup_lua_tagtransform(context):
    if not context.config.userdata['HAVE_LUA']:
        context.scenario.skip("Lua support not compiled in.")
        return

    context.osm2pgsql_params.extend(('--tag-transform-script',
                                    str(context.default_data_dir / 'style.lua')))

@given("the lua style")
def setup_inline_lua_style(context):
    if not context.config.userdata['HAVE_LUA']:
        context.scenario.skip("Lua support not compiled in.")
        return

    outfile = context.workdir / 'inline_style.lua'
    outfile.write_text(context.text)
    context.osm2pgsql_params.extend(('-S', str(outfile)))


@given("the style file '(?P<style>.+)'")
def setup_style_file(context, style):
    context.osm2pgsql_params.extend(('-S', str(context.test_data_dir / style)))


@when("running osm2pgsql (?P<output>\w+)(?: with parameters)?")
def execute_osm2pgsql_sucessfully(context, output):
    returncode = run_osm2pgsql(context, output)

    if context.scenario.status == "skipped":
        return

    assert returncode == 0,\
           f"osm2psql failed with error code {returncode}.\n"\
           f"Output:\n{context.osm2psql_outdata[0]}\n{context.osm2psql_outdata[1]}\n"


@then("running osm2pgsql (?P<output>\w+)(?: with parameters)? fails")
def execute_osm2pgsql_with_failure(context, output):
    returncode = run_osm2pgsql(context, output)

    if context.scenario.status == "skipped":
        return

    assert returncode != 0, "osm2pgsql unexpectedly succeeded"


@then("the (?P<kind>\w+) output contains")
def check_program_output(context, kind):
    if kind == 'error':
        s = context.osm2psql_outdata[1]
    elif kind == 'standard':
        s = context.osm2psql_outdata[0]
    else:
        assert not "Expect one of error, standard"

    for line in context.text.split('\n'):
        line = line.strip()
        if line:
            assert line in s,\
                   f"Output '{line}' not found in {kind} output:\n{s}\n"
