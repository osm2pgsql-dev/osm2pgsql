# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2023 by the osm2pgsql developer community.
# For a full list of authors see the git log.
"""
Steps for executing osm2pgsql.
"""
from io import StringIO
from pathlib import Path
import sys
import subprocess
import contextlib
import logging
import datetime as dt

from osmium.replication.server import OsmosisState

def get_import_file(context):
    if context.import_file is not None:
        return str(context.import_file), None

    context.geometry_factory.complete_node_list(context.import_data['n'])

    # sort by OSM id
    for obj in context.import_data.values():
        obj.sort(key=lambda l: int(l.split(' ')[0][1:]))

    fd = StringIO()
    for typ in ('n', 'w', 'r'):
        for line in context.import_data[typ]:
            fd.write(line)
            fd.write('\n')
        context.import_data[typ].clear()

    return '-', fd.getvalue()


def run_osm2pgsql(context, output):
    assert output in ('flex', 'pgsql', 'gazetteer', 'none')

    cmdline = [str(Path(context.config.userdata['BINARY']).resolve())]
    cmdline.extend(('-O', output))
    cmdline.extend(context.osm2pgsql_params)

    # convert table items to CLI arguments and inject constants to placeholders
    if context.table:
        cmdline.extend(f.format(**context.config.userdata) for f in context.table.headings if f)
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

    data_file, data_stdin = get_import_file(context)

    if data_stdin is not None:
        data_stdin = data_stdin.encode('utf-8')
        cmdline.extend(('-r', 'opl'))

    cmdline.append(data_file)

    proc = subprocess.Popen(cmdline, cwd=str(context.workdir),
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    outdata = proc.communicate(input=data_stdin)

    context.osm2pgsql_outdata = [d.decode('utf-8').replace('\\n', '\n') for d in outdata]

    return proc.returncode


def run_osm2pgsql_replication(context):
    cmdline = []
    # convert table items to CLI arguments and inject constants to placeholders
    if context.table:
        cmdline.extend(f.format(**context.config.userdata) for f in context.table.headings if f)
        for row in context.table:
            cmdline.extend(f.format(**context.config.userdata) for f in row if f)

    if '-d' not in cmdline and '--database' not in cmdline:
        cmdline.extend(('-d', context.config.userdata['TEST_DB']))

    if cmdline[0] == 'update':
        cmdline.extend(('--osm2pgsql-cmd',
                        str(Path(context.config.userdata['BINARY']).resolve())))

        if '--' not in cmdline:
            cmdline.extend(('--', '-S', str(context.default_data_dir / 'default.style')))


    serr = StringIO()
    log_handler = logging.StreamHandler(serr)
    context.osm2pgsql_replication.LOG.addHandler(log_handler)
    with contextlib.redirect_stdout(StringIO()) as sout:
        retval = context.osm2pgsql_replication.main(cmdline)
    context.osm2pgsql_replication.LOG.removeHandler(log_handler)

    context.osm2pgsql_outdata = [sout.getvalue(), serr.getvalue()]
    print(context.osm2pgsql_outdata)

    return retval


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
    if style.endswith('.lua') and not context.config.userdata['HAVE_LUA']:
        context.scenario.skip("Lua support not compiled in.")
        return

    context.osm2pgsql_params.extend(('-S', str(context.test_data_dir / style)))


@when("running osm2pgsql (?P<output>\w+)(?: with parameters)?")
def execute_osm2pgsql_successfully(context, output):
    returncode = run_osm2pgsql(context, output)

    if context.scenario.status == "skipped":
        return

    assert returncode == 0,\
           f"osm2pgsql failed with error code {returncode}.\n"\
           f"Output:\n{context.osm2pgsql_outdata[0]}\n{context.osm2pgsql_outdata[1]}\n"


@then("running osm2pgsql (?P<output>\w+)(?: with parameters)? fails")
def execute_osm2pgsql_with_failure(context, output):
    returncode = run_osm2pgsql(context, output)

    if context.scenario.status == "skipped":
        return

    assert returncode != 0, "osm2pgsql unexpectedly succeeded"


@when("running osm2pgsql-replication")
def execute_osm2pgsql_replication_successfully(context):
    returncode = run_osm2pgsql_replication(context)

    assert returncode == 0,\
           f"osm2pgsql-replication failed with error code {returncode}.\n"\
           f"Output:\n{context.osm2pgsql_outdata[0]}\n{context.osm2pgsql_outdata[1]}\n"


@then("running osm2pgsql-replication fails(?: with returncode (?P<expected>\d+))?")
def execute_osm2pgsql_replication_successfully(context, expected):
    returncode = run_osm2pgsql_replication(context)

    assert returncode != 0, "osm2pgsql-replication unexpectedly succeeded"
    if expected:
        assert returncode == int(expected), \
               f"osm2pgsql-replication failed with returncode {returncode} instead of {expected}."\
               f"Output:\n{context.osm2pgsql_outdata[0]}\n{context.osm2pgsql_outdata[1]}\n"


@then("the (?P<kind>\w+) output contains")
def check_program_output(context, kind):
    if kind == 'error':
        s = context.osm2pgsql_outdata[1]
    elif kind == 'standard':
        s = context.osm2pgsql_outdata[0]
    else:
        assert not "Expect one of error, standard"

    for line in context.text.split('\n'):
        line = line.strip()
        if line:
            assert line in s,\
                   f"Output '{line}' not found in {kind} output:\n{s}\n"


@given("the replication service at (?P<base_url>.*)")
def setup_replication_mock(context, base_url):
    context.osm2pgsql_replication.ReplicationServer.expected_base_url = base_url
    if context.table:
        context.osm2pgsql_replication.ReplicationServer.state_infos =\
          [OsmosisState(int(row[0]),
                        dt.datetime.strptime(row[1], '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=dt.timezone.utc))
           for row in context.table]


@given("the URL (?P<base_url>.*) returns")
def mock_url_response(context, base_url):
    context.urlrequest_responses[base_url] = context.text
