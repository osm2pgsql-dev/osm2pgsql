#ifndef OSM2PGSQL_FLEX_TILESET_HPP
#define OSM2PGSQL_FLEX_TILESET_HPP

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
#include <vector>

/**
 * A tileset for the flex output. Used for expire.
 */
class flex_tileset_t
{
public:
    explicit flex_tileset_t(std::string name) : m_name(std::move(name)) {}

    std::string const &name() const noexcept { return m_name; }

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

private:
    /// The name of the tileset
    std::string m_name;

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

}; // class flex_tileset_t

#endif // OSM2PGSQL_FLEX_TILESET_HPP
