#ifndef OSM2PGSQL_GEN_DISCRETE_ISOLATION_HPP
#define OSM2PGSQL_GEN_DISCRETE_ISOLATION_HPP

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

class gen_di_t : public gen_base_t
{
public:
    gen_di_t(pg_conn_t *connection, params_t *params);

    void process() override;

    std::string_view strategy() const noexcept override
    {
        return "discrete-isolation";
    }

private:
    std::size_t m_timer_get;
    std::size_t m_timer_sort;
    std::size_t m_timer_di;
    std::size_t m_timer_reorder;
    std::size_t m_timer_write;
};

#endif // OSM2PGSQL_GEN_DISCRETE_ISOLATION_HPP
