#ifndef OSM2PGSQL_GEN_TILE_SQL_HPP
#define OSM2PGSQL_GEN_TILE_SQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile.hpp"

#include <string>
#include <string_view>

class gen_tile_sql_t final : public gen_tile_t
{
public:
    gen_tile_sql_t(pg_conn_t *connection, bool append, params_t *params);

    ~gen_tile_sql_t() override = default;

    void process(tile_t const &tile) override;

    void post() override;

    std::string_view strategy() const noexcept override { return "tile-sql"; }

private:
    std::string m_sql_template;
};

#endif // OSM2PGSQL_GEN_TILE_SQL_HPP
