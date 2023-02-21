#ifndef OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP
#define OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-output.hpp"

#include <vector>

struct lua_State;

static char const *const osm2pgsql_expire_output_name =
    "osm2pgsql.ExpireOutput";

int setup_flex_expire_output(lua_State *lua_state,
                             std::vector<expire_output_t> *expire_outputs);

#endif // OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP
