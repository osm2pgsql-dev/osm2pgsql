/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-index.hpp"
#include "util.hpp"

std::string flex_index_t::columns() const
{
    return util::join(m_columns, ',', '"', '(', ')');
}

std::string flex_index_t::include_columns() const
{
    return util::join(m_include_columns, ',', '"', '(', ')');
}

std::string
flex_index_t::create_index(std::string const &qualified_table_name) const
{
    util::string_joiner_t joiner{' '};
    joiner.add("CREATE");

    if (m_is_unique) {
        joiner.add("UNIQUE");
    }

    joiner.add("INDEX ON");
    joiner.add(qualified_table_name);

    joiner.add("USING");
    joiner.add(m_method);

    if (m_expression.empty()) {
        joiner.add(columns());
    } else {
        joiner.add('(' + m_expression + ')');
    }

    if (!m_include_columns.empty()) {
        joiner.add("INCLUDE");
        joiner.add(include_columns());
    }

    if (m_fillfactor != 0) {
        joiner.add("WITH");
        joiner.add(fmt::format("(fillfactor = {})", m_fillfactor));
    }

    if (!m_tablespace.empty()) {
        joiner.add("TABLESPACE");
        joiner.add("\"" + m_tablespace + "\"");
    }

    if (!m_where_condition.empty()) {
        joiner.add("WHERE");
        joiner.add(m_where_condition);
    }

    return joiner();
}
