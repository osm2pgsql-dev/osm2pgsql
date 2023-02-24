#ifndef OSM2PGSQL_EXPIRE_OUTPUT_HPP
#define OSM2PGSQL_EXPIRE_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tile.hpp"

#include <string>
#include <utility>

class pg_conn_t;

/**
 * Output for tile expiry.
 */
class expire_output_t
{
public:
    expire_output_t() = default;

    std::string filename() const noexcept { return m_filename; }

    void set_filename(std::string filename)
    {
        m_filename = std::move(filename);
    }

    std::string schema() const noexcept { return m_schema; }

    std::string table() const noexcept { return m_table; }

    void set_schema_and_table(std::string schema, std::string table)
    {
        m_schema = std::move(schema);
        m_table = std::move(table);
    }

    uint32_t minzoom() const noexcept { return m_minzoom; }
    void set_minzoom(uint32_t minzoom) noexcept { m_minzoom = minzoom; }

    uint32_t maxzoom() const noexcept { return m_maxzoom; }
    void set_maxzoom(uint32_t maxzoom) noexcept { m_maxzoom = maxzoom; }

    std::size_t output(quadkey_list_t const &tile_list,
                       std::string const &conninfo) const;

    /**
     * Write the list of tiles to a file.
     *
     * \param tiles_at_maxzoom The list of tiles at maximum zoom level
     */
    std::size_t
    output_tiles_to_file(quadkey_list_t const &tiles_at_maxzoom) const;

    /**
     * Write the list of tiles to a database table.
     *
     * \param tiles_at_maxzoom The list of tiles at maximum zoom level
     * \param conninfo database connection info
     */
    std::size_t output_tiles_to_table(quadkey_list_t const &tiles_at_maxzoom,
                                      std::string const &conninfo) const;

    /**
     * Create table for tiles.
     */
    void create_output_table(pg_conn_t const &connection) const;

private:
    /// The filename (if any) for output
    std::string m_filename;

    /// The schema (if any) for output
    std::string m_schema;

    /// The table (if any) for output
    std::string m_table;

    /// Minimum zoom level for output
    uint32_t m_minzoom = 0;

    /// Zoom level we capture tiles on
    uint32_t m_maxzoom = 0;

}; // class expire_output_t

#endif // OSM2PGSQL_EXPIRE_OUTPUT_HPP
