#ifndef OSM2PGSQL_GEN_TILE_RASTER_HPP
#define OSM2PGSQL_GEN_TILE_RASTER_HPP

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

class gen_tile_raster_union_t final : public gen_tile_t
{
public:
    gen_tile_raster_union_t(pg_conn_t *connection, params_t *params);

    ~gen_tile_raster_union_t() override = default;

    void process(tile_t const &tile) override;

    void post() override;

    std::string_view strategy() const noexcept override
    {
        return "raster-union";
    }

private:
    std::size_t m_timer_draw;
    std::size_t m_timer_simplify;
    std::size_t m_timer_vectorize;
    std::size_t m_timer_write;

    std::string m_image_path;
    std::string m_image_table;
    double m_margin = 0.0;
    std::size_t m_image_extent = 2048;
    std::size_t m_image_buffer = 0;
    unsigned int m_buffer_size = 10;
    int m_turdsize = 2;
};

#endif // OSM2PGSQL_GEN_TILE_RASTER_HPP
