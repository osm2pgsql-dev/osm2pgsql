#ifndef OSM2PGSQL_GEN_CREATE_HPP
#define OSM2PGSQL_GEN_CREATE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-base.hpp"

#include <memory>
#include <string>

class params_t;
class pg_conn_t;

/// Instantiate a generalizer for the specified strategy.
std::unique_ptr<gen_base_t> create_generalizer(std::string const &strategy,
                                               pg_conn_t *connection,
                                               params_t *params);

#endif // OSM2PGSQL_GEN_CREATE_HPP
