#ifndef OSM2PGSQL_EXPIRE_OUTPUT_HPP
#define OSM2PGSQL_EXPIRE_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tile.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

constexpr std::size_t DEFAULT_MAX_TILES_GEOMETRY = 10'000'000;
constexpr std::size_t DEFAULT_MAX_TILES_OVERALL = 50'000'000;

class pg_conn_t;
class connection_params_t;

/**
 * Output for tile expiry.
 */
class expire_output_t
{
public:
    expire_output_t() = default;

    std::string const &filename() const noexcept { return m_filename; }

    void set_filename(std::string filename)
    {
        m_filename = std::move(filename);
    }

    std::string const &schema() const noexcept { return m_schema; }

    std::string const &table() const noexcept { return m_table; }

    void set_schema_and_table(std::string schema, std::string table)
    {
        assert(!schema.empty());
        m_schema = std::move(schema);
        m_table = std::move(table);
    }

    uint32_t minzoom() const noexcept { return m_minzoom; }
    void set_minzoom(uint32_t minzoom) noexcept { m_minzoom = minzoom; }

    uint32_t maxzoom() const noexcept { return m_maxzoom; }
    void set_maxzoom(uint32_t maxzoom) noexcept { m_maxzoom = maxzoom; }

    std::size_t max_tiles_geometry() const noexcept
    {
        return m_max_tiles_geometry;
    }

    void set_max_tiles_geometry(std::size_t max_tiles_geometry) noexcept
    {
        m_max_tiles_geometry = max_tiles_geometry;
    }

    std::size_t max_tiles_overall() const noexcept
    {
        return m_max_tiles_overall;
    }

    void set_max_tiles_overall(std::size_t max_tiles_overall) noexcept
    {
        m_max_tiles_overall = max_tiles_overall;
    }

    bool empty() noexcept;

    void add_tiles(std::unordered_set<quadkey_t> const &dirty_tiles);

    quadkey_list_t get_tiles();

    /**
     * Write the list of tiles to a database table or file.
     *
     * \param connection_params Database connection parameters
     */
    std::size_t output(connection_params_t const &connection_params);

    /**
     * Create table for tiles.
     */
    void create_output_table(pg_conn_t const &db_connection) const;

private:
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
     * \param connection_params Database connection parameters
     */
    std::size_t
    output_tiles_to_table(quadkey_list_t const &tiles_at_maxzoom,
                          connection_params_t const &connection_params) const;

    /**
     * Access to the m_tiles collection of expired tiles must go through
     * this mutex, because it can happend from several threads at the same
     * time. Mutex is wrapped in a shared_ptr to make this class movable so
     * we can store instances in std::vector.
     */
    std::shared_ptr<std::mutex> m_tiles_mutex = std::make_shared<std::mutex>();

    /// This is where we collect all the expired tiles.
    std::unordered_set<quadkey_t> m_tiles;

    /// The filename (if any) for output
    std::string m_filename;

    /// The schema for output
    std::string m_schema;

    /// The table (if any) for output
    std::string m_table;

    /// Minimum zoom level for output
    uint32_t m_minzoom = 0;

    /// Zoom level we capture tiles on
    uint32_t m_maxzoom = 0;

    /**
     * The following two settings are for protecting osm2pgsql from overload as
     * well as downstream tile expiry mechanisms in case of large changes to
     * OSM data (possibly from vandalism). They should be large enough to not
     * trigger in normal use.
     */

    /// Maximum number of tiles that can be affected by a single geometry.
    std::size_t m_max_tiles_geometry = DEFAULT_MAX_TILES_GEOMETRY;

    /// Maximum number of tiles that can be affected per run.
    std::size_t m_max_tiles_overall = DEFAULT_MAX_TILES_OVERALL;

    /// Has the overall tile limit been reached already.
    bool m_overall_tile_limit_reached = false;

}; // class expire_output_t

#endif // OSM2PGSQL_EXPIRE_OUTPUT_HPP
