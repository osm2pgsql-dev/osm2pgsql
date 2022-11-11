/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile.hpp"

#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

#include <cstdlib>

gen_tile_t::gen_tile_t(pg_conn_t *connection, params_t *params)
: gen_base_t(connection, params), m_timer_delete(add_timer("delete"))
{
    m_with_group_by = !get_params().get_identifier("group_by_column").empty();

    if (get_params().get_bool("delete_existing")) {
        m_delete_existing = true;
        dbexec("PREPARE del_geoms (int, int) AS"
               " DELETE FROM {dest} WHERE x=$1 AND y=$2");
    }
}

void gen_tile_t::delete_existing(tile_t const &tile)
{
    if (!m_delete_existing) {
        return;
    }

    if (debug()) {
        log_gen("Delete geometries from destination table...");
    }

    timer(m_timer_delete).start();
    auto const result =
        connection().exec_prepared("del_geoms", tile.x(), tile.y());
    timer(m_timer_delete).stop();

    if (debug()) {
        log_gen("Deleted {} rows.", result.affected_rows());
    }
}
