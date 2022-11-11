#ifndef OSM2PGSQL_GEN_TILE_BUILTUP_HPP
#define OSM2PGSQL_GEN_TILE_BUILTUP_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile.hpp"

#include <string>
#include <string_view>
#include <vector>

class gen_tile_builtup_t final : public gen_tile_t
{
public:
    gen_tile_builtup_t(pg_conn_t *connection, params_t *params);

    ~gen_tile_builtup_t() override = default;

    void process(tile_t const &tile) override;

    void post() override;

    std::string_view strategy() const noexcept override { return "builtup"; }

private:
    std::size_t m_timer_draw;
    std::size_t m_timer_simplify;
    std::size_t m_timer_vectorize;
    std::size_t m_timer_write;

    std::vector<std::string> m_source_tables;
    std::string m_image_path;
    std::string m_schema;
    std::string m_dest_table;
    std::string m_image_table;
    double m_margin = 0.0;
    std::size_t m_image_extent = 2048;
    std::size_t m_image_buffer = 0;
    std::vector<unsigned int> m_buffer_sizes;
    int m_turdsize = 2;
    double m_min_area = 0.0;
    bool m_has_area_column;
};

#endif // OSM2PGSQL_GEN_TILE_BUILTUP_HPP
