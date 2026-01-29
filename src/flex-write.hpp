#ifndef OSM2PGSQL_FLEX_WRITE_HPP
#define OSM2PGSQL_FLEX_WRITE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table.hpp"

#include <lua.hpp>

#include <stdexcept>
#include <string>
#include <vector>

class expire_tiles_t;

class not_null_exception_t : public std::runtime_error
{
public:
    not_null_exception_t(std::string const &message,
                       flex_table_column_t const *column)
    : std::runtime_error(message), m_column(column)
    {}

    flex_table_column_t const &column() const noexcept { return *m_column; }

private:
    flex_table_column_t const *m_column;
}; // class not_null_exception_t

void flex_write_column(lua_State *lua_state,
                       db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                       flex_table_column_t const &column,
                       std::vector<expire_tiles_t> *expire);

#endif // OSM2PGSQL_FLEX_WRITE_HPP
