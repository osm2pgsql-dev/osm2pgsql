#ifndef OSM2PGSQL_COMMAND_LINE_PARSER_HPP
#define OSM2PGSQL_COMMAND_LINE_PARSER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "options.hpp"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
options_t parse_command_line(int argc, char *argv[]);

void print_version();

#endif // OSM2PGSQL_COMMAND_LINE_PARSER_HPP
