/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-output.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

#include <cerrno>
#include <system_error>

std::size_t
expire_output_t::output(quadkey_list_t const &tile_list,
                        connection_params_t const &connection_params) const
{
    std::size_t num = 0;
    if (!m_filename.empty()) {
        num = output_tiles_to_file(tile_list);
    }
    if (!m_table.empty()) {
        num = output_tiles_to_table(tile_list, connection_params);
    }
    return num;
}

std::size_t expire_output_t::output_tiles_to_file(
    quadkey_list_t const &tiles_at_maxzoom) const
{
    FILE *outfile = std::fopen(m_filename.data(), "a");
    if (outfile == nullptr) {
        std::system_error const error{errno, std::generic_category()};
        log_warn("Failed to open expired tiles file ({}). Tile expiry "
                 "list will not be written!",
                 error.code().message());
        return 0;
    }

    auto const count = for_each_tile(
        tiles_at_maxzoom, m_minzoom, m_maxzoom, [&](tile_t const &tile) {
            fmt::print(outfile, "{}\n", tile.to_zxy());
        });

    (void)std::fclose(outfile);

    return count;
}

std::size_t expire_output_t::output_tiles_to_table(
    quadkey_list_t const &tiles_at_maxzoom,
    connection_params_t const &connection_params) const
{
    auto const qn = qualified_name(m_schema, m_table);

    pg_conn_t const db_connection{connection_params, "expire"};

    auto const result = db_connection.exec("SELECT * FROM {} LIMIT 1", qn);

    if (result.num_fields() == 3) {
        // old format with fields: zoom, x, y
        db_connection.prepare("insert_tiles",
                              "INSERT INTO {} (zoom, x, y)"
                              " VALUES ($1::int4, $2::int4, $3::int4)"
                              " ON CONFLICT DO NOTHING",
                              qn);
    } else {
        // new format with fields: zoom, x, y, first, last
        db_connection.prepare("insert_tiles",
                              "INSERT INTO {} (zoom, x, y)"
                              " VALUES ($1::int4, $2::int4, $3::int4)"
                              " ON CONFLICT (zoom, x, y)"
                              " DO UPDATE SET last = CURRENT_TIMESTAMP(0)",
                              qn);
    }

    auto const count = for_each_tile(
        tiles_at_maxzoom, m_minzoom, m_maxzoom, [&](tile_t const &tile) {
            db_connection.exec_prepared("insert_tiles", tile.zoom(), tile.x(),
                                        tile.y());
        });

    return count;
}

void expire_output_t::create_output_table(pg_conn_t const &db_connection) const
{
    auto const qn = qualified_name(m_schema, m_table);
    db_connection.exec(
        "CREATE TABLE IF NOT EXISTS {} ("
        " zoom int4 NOT NULL,"
        " x int4 NOT NULL,"
        " y int4 NOT NULL,"
        " first timestamp with time zone DEFAULT CURRENT_TIMESTAMP(0),"
        " last timestamp with time zone DEFAULT CURRENT_TIMESTAMP(0),"
        " PRIMARY KEY (zoom, x, y))",
        qn);
}
