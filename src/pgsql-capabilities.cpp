/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "pgsql.hpp"
#include "pgsql-helper.hpp"

#include <set>
#include <string>

static std::set<std::string> init_set_from_table(pg_conn_t const &db_connection,
                                                 char const *table,
                                                 char const *column,
                                                 char const *condition)
{
    std::set<std::string> values;

    auto const res = db_connection.query(
        PGRES_TUPLES_OK,
        "SELECT {} FROM {} WHERE {}"_format(column, table, condition));
    for (int i = 0; i < res.num_tuples(); ++i) {
        values.insert(res.get_value_as_string(i, 0));
    }

    return values;
}

bool has_extension(pg_conn_t const &db_connection, std::string const &value)
{
    static const std::set<std::string> values = init_set_from_table(
        db_connection, "pg_catalog.pg_extension", "extname", "true");

    return values.count(value);
}

bool has_schema(pg_conn_t const &db_connection, std::string const &value)
{
    static const std::set<std::string> values = init_set_from_table(
        db_connection, "pg_catalog.pg_namespace", "nspname",
        "nspname !~ '^pg_' AND nspname <> 'information_schema'");

    if (value.empty()) {
        return true;
    }

    return values.count(value);
}

bool has_tablespace(pg_conn_t const &db_connection, std::string const &value)
{
    static const std::set<std::string> values =
        init_set_from_table(db_connection, "pg_catalog.pg_tablespace",
                            "spcname", "spcname != 'pg_global'");

    if (value.empty()) {
        return true;
    }

    return values.count(value);
}
