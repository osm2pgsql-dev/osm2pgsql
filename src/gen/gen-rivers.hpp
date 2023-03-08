#ifndef OSM2PGSQL_GEN_RIVERS_HPP
#define OSM2PGSQL_GEN_RIVERS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-base.hpp"

#include <string_view>

class gen_rivers_t : public gen_base_t
{
public:
    gen_rivers_t(pg_conn_t *connection, params_t *params);

    void process() override;

    std::string_view strategy() const noexcept override { return "rivers"; }

private:
    void get_stats();

    std::size_t m_timer_area;
    std::size_t m_timer_prep;
    std::size_t m_timer_get;
    std::size_t m_timer_sort;
    std::size_t m_timer_net;
    std::size_t m_timer_remove;
    std::size_t m_timer_width;
    std::size_t m_timer_write;

    std::size_t m_num_waterways = 0;
    std::size_t m_num_points = 0;
    bool m_delete_existing;
};

#endif // OSM2PGSQL_GEN_RIVERS_HPP
