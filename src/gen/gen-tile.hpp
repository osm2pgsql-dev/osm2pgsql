#ifndef OSM2PGSQL_GEN_TILE_HPP
#define OSM2PGSQL_GEN_TILE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-base.hpp"

/**
 * Base class for generalizations based on tiles.
 */
class gen_tile_t : public gen_base_t
{
public:
    bool on_tiles() const noexcept override { return true; }

    uint32_t get_zoom() const noexcept override { return m_zoom; }

protected:
    gen_tile_t(pg_conn_t *connection, params_t *params);

    uint32_t parse_zoom();

    void delete_existing(tile_t const &tile);

    bool with_group_by() const noexcept { return m_with_group_by; }

private:
    std::size_t m_timer_delete;
    int32_t m_zoom;
    bool m_delete_existing = false;
    bool m_with_group_by = false;
};

#endif // OSM2PGSQL_GEN_TILE_HPP
