#ifndef OSM2PGSQL_FLEX_INDEX_HPP
#define OSM2PGSQL_FLEX_INDEX_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * This class represents a database index.
 */
class flex_index_t
{
public:
    explicit flex_index_t(std::string method) : m_method(std::move(method)) {}

    std::string const &method() const noexcept { return m_method; }

    std::string columns() const;

    /// Set columns (single-column version)
    void set_columns(std::string const &columns)
    {
        assert(m_columns.empty());
        m_columns.push_back(columns);
    }

    /// Set columns (multi-column version)
    void set_columns(std::vector<std::string> const &columns)
    {
        m_columns = columns;
    }

    std::string include_columns() const;

    void set_include_columns(std::vector<std::string> const &columns)
    {
        m_include_columns = columns;
    }

    std::string const &expression() const noexcept { return m_expression; }

    void set_expression(std::string expression)
    {
        m_expression = std::move(expression);
    }

    std::string const &tablespace() const noexcept { return m_tablespace; }

    void set_tablespace(std::string tablespace)
    {
        m_tablespace = std::move(tablespace);
    }

    std::string const &where_condition() const noexcept
    {
        return m_where_condition;
    }

    void set_where_condition(std::string where_condition)
    {
        m_where_condition = std::move(where_condition);
    }

    void set_fillfactor(uint8_t fillfactor)
    {
        if (fillfactor < 10 || fillfactor > 100) {
            throw std::runtime_error{"Fillfactor must be between 10 and 100."};
        }
        m_fillfactor = fillfactor;
    }

    bool is_unique() const noexcept { return m_is_unique; }

    void set_is_unique(bool unique) noexcept { m_is_unique = unique; }

    std::string create_index(std::string const &qualified_table_name) const;

private:
    std::vector<std::string> m_columns;
    std::vector<std::string> m_include_columns;
    std::string m_method;
    std::string m_expression;
    std::string m_tablespace;
    std::string m_where_condition;
    uint8_t m_fillfactor = 0;
    bool m_is_unique = false;

}; // class flex_index_t

#endif // OSM2PGSQL_FLEX_INDEX_HPP
