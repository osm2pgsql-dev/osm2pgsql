/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-create.hpp"

#include "gen-discrete-isolation.hpp"
#include "gen-rivers.hpp"
#include "gen-tile-builtup.hpp"
#include "gen-tile-raster.hpp"
#include "gen-tile-vector.hpp"
#include "params.hpp"

std::unique_ptr<gen_base_t> create_generalizer(std::string const &strategy,
                                               pg_conn_t *connection,
                                               params_t *params)
{
    auto generalizer = [&]() -> std::unique_ptr<gen_base_t> {
        if (strategy == "builtup") {
            return std::make_unique<gen_tile_builtup_t>(connection, params);
        }
        if (strategy == "discrete-isolation") {
            return std::make_unique<gen_di_t>(connection, params);
        }
        if (strategy == "raster-union") {
            return std::make_unique<gen_tile_raster_union_t>(connection,
                                                             params);
        }
        if (strategy == "rivers") {
            return std::make_unique<gen_rivers_t>(connection, params);
        }
        if (strategy == "vector-union") {
            return std::make_unique<gen_tile_vector_union_t>(connection,
                                                             params);
        }
        throw fmt_error("Unknown generalization strategy '{}'.", strategy);
    }();

    return generalizer;
}
