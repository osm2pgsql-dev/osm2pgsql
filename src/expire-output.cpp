/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-output.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

#include <system_error>

std::size_t expire_output_t::output(quadkey_list_t const &tile_list,
                                    std::string const &conninfo) const
{
    std::size_t num = 0;
    if (!m_filename.empty()) {
        num = output_tiles_to_file(tile_list);
    }
    if (!m_table.empty()) {
        num = output_tiles_to_table(tile_list, conninfo);
    }
    return num;
}

std::size_t expire_output_t::output_tiles_to_file(
    quadkey_list_t const &tiles_at_maxzoom) const
{
    FILE *outfile = std::fopen(m_filename.data(), "a");
    if (outfile == nullptr) {
        std::system_error error{errno, std::generic_category()};
        log_warn("Failed to open expired tiles file ({}). Tile expiry "
                 "list will not be written!",
                 error.code().message());
        return 0;
    }

    auto const count = for_each_tile(
        tiles_at_maxzoom, m_minzoom, m_maxzoom, [&](tile_t const &tile) {
            fmt::print(outfile, "{}/{}/{}\n", tile.zoom(), tile.x(), tile.y());
        });

    (void)std::fclose(outfile);

    return count;
}

std::size_t
expire_output_t::output_tiles_to_table(quadkey_list_t const &tiles_at_maxzoom,
                                       std::string const &conninfo) const
{
    auto const qn = qualified_name(m_schema, m_table);

    pg_conn_t connection{conninfo};

    connection.exec("PREPARE insert_tiles(int4, int4, int4) AS"
                    " INSERT INTO {} (zoom, x, y) VALUES ($1, $2, $3)"
                    " ON CONFLICT DO NOTHING",
                    qn);

    auto const count = for_each_tile(
        tiles_at_maxzoom, m_minzoom, m_maxzoom, [&](tile_t const &tile) {
            connection.exec_prepared("insert_tiles", tile.zoom(), tile.x(),
                                     tile.y());
        });

    return count;
}

void expire_output_t::create_output_table(pg_conn_t const &connection) const
{
    auto const qn = qualified_name(m_schema, m_table);
    connection.exec("CREATE TABLE IF NOT EXISTS {} ("
                    " zoom int4 NOT NULL,"
                    " x int4 NOT NULL,"
                    " y int4 NOT NULL,"
                    " PRIMARY KEY (zoom, x, y))",
                    qn);
}
