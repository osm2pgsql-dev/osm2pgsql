# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2022 by the osm2pgsql developer community.
# For a full list of authors see the git log.
""" Create a man page for osm2pgsql helper scripts in python.
"""
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import re

from build_manpages.manpage import Manpage
from build_manpages.build_manpage import get_parser_from_file

SEE_ALSO = [
 "osm2pgsql website (https://osm2pgsql.org)",
 "osm2pgsql manual (https://osm2pgsql.org/doc/manual.html)"
]

def create_manpage_for(args):
    """ Create a man page for the given script.
    """
    parser = get_parser_from_file(args.script, 'get_parser', 'function')
    parser.man_short_description = args.description
    parser._manpage = [
      {'heading': 'SEE ALSO',
       'content': '\n'.join(f"* {s}" for s in SEE_ALSO)}
    ]

    manpage = str(Manpage(parser))
    manpage = re.sub(r'.TH.*',
                     f'.TH "{parser.prog.upper()}" "1" "{args.version}" "" ""',
                     manpage)
    # Correct quoting for single quotes. See groff manpage.
    manpage = manpage.replace('`', '\\(cq')

    return manpage


if __name__ == "__main__":
    parser = ArgumentParser(usage='%(prog)s [options] <script>',
                            formatter_class=RawDescriptionHelpFormatter,
                            description=__doc__)
    parser.add_argument('--version', default="Manual",
                        help='Version of the software')
    parser.add_argument('--description',
                        help='Short description added to the name')
    parser.add_argument('script',
                        help='Name of the script to generate the manpage for.')

    args = parser.parse_args()

    manpage = create_manpage_for(args)

    print(manpage)
