/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile-sql.hpp"

#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

gen_tile_sql_t::gen_tile_sql_t(pg_conn_t *connection, bool append,
                               params_t *params)
: gen_tile_t(connection, append, params),
  m_sql_template(get_params().get_string("sql"))
{
    check_src_dest_table_params_exist();
}

void gen_tile_sql_t::process(tile_t const &tile)
{
    connection().exec("BEGIN");
    delete_existing(tile);

    log_gen("Run SQL...");

    params_t tmp_params;
    tmp_params.set("ZOOM", tile.zoom());
    tmp_params.set("X", tile.x());
    tmp_params.set("Y", tile.y());
    dbexec(tmp_params, m_sql_template);

    connection().exec("COMMIT");
    log_gen("Done.");
}

void gen_tile_sql_t::post()
{
    if (!append_mode()) {
        dbexec("ANALYZE {dest}");
    }
}
