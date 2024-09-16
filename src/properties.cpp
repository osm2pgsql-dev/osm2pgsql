/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "properties.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"

#include <cassert>
#include <cstdlib>

namespace {

constexpr char const *const properties_table = "osm2pgsql_properties";

} // anonymous namespace

properties_t::properties_t(connection_params_t connection_params,
                           std::string schema)
: m_connection_params(std::move(connection_params)),
  m_schema(std::move(schema)),
  m_has_properties_table(has_table(m_schema, properties_table))
{
    assert(!m_schema.empty());
    log_debug("Found properties table '{}': {}.", properties_table,
              m_has_properties_table);
}

std::string properties_t::get_string(std::string const &property,
                                     std::string const &default_value) const
{
    auto const it = m_properties.find(property);
    if (it == m_properties.end()) {
        return default_value;
    }

    return it->second;
}

int64_t properties_t::get_int(std::string const &property,
                              int64_t default_value) const
{
    auto const it = m_properties.find(property);
    if (it == m_properties.end()) {
        return default_value;
    }

    char *end = nullptr;
    errno = 0;
    int64_t const val = std::strtol(it->second.data(), &end, 10);
    if (errno || *end != '\0') {
        throw fmt_error("Corruption in properties: '{}' must be an integer.",
                        property);
    }

    return val;
}

bool properties_t::get_bool(std::string const &property,
                            bool default_value) const
{
    auto const it = m_properties.find(property);
    if (it == m_properties.end()) {
        return default_value;
    }

    if (it->second == "true") {
        return true;
    }
    if (it->second == "false") {
        return false;
    }

    throw fmt_error("Corruption in properties: '{}' must be 'true' or 'false'.",
                    property);
}

void properties_t::set_string(std::string property, std::string value)
{
    m_properties[property] = value;
    m_to_update[property] = value;
}

void properties_t::set_int(std::string property, int64_t value)
{
    set_string(std::move(property), std::to_string(value));
}

void properties_t::set_bool(std::string property, bool value)
{
    set_string(std::move(property), value ? "true" : "false");
}

void properties_t::init_table()
{
    auto const table = table_name();
    log_info("Initializing properties table '{}'.", table);

    pg_conn_t const db_connection{m_connection_params, "prop.store"};
    db_connection.exec("CREATE TABLE IF NOT EXISTS {} ("
                       " property TEXT NOT NULL PRIMARY KEY,"
                       " value TEXT NOT NULL)",
                       table);
    db_connection.exec("TRUNCATE {}", table);
    m_has_properties_table = true;
}

void properties_t::store()
{
    auto const table = table_name();
    log_info("Storing properties to table '{}'.", table);

    pg_conn_t const db_connection{m_connection_params, "prop.store"};

    db_connection.exec(
        "PREPARE set_property(text, text) AS"
        " INSERT INTO {} (property, value) VALUES ($1, $2)"
        " ON CONFLICT (property) DO UPDATE SET value = EXCLUDED.value",
        table);

    for (auto const &[k, v] : m_to_update) {
        log_debug("  Storing {}='{}'", k, v);
        db_connection.exec_prepared("set_property", k, v);
    }

    m_to_update.clear();
}

bool properties_t::load()
{
    if (!m_has_properties_table) {
        log_info("No properties found in database from previous import.");
        return false;
    }

    m_properties.clear();

    auto const table = table_name();
    log_info("Loading properties from table '{}'.", table);

    pg_conn_t const db_connection{m_connection_params, "prop.load"};
    auto const result = db_connection.exec("SELECT * FROM {}", table);

    for (int i = 0; i < result.num_tuples(); ++i) {
        m_properties.insert_or_assign(result.get_value(i, 0), result.get(i, 1));
    }

    return true;
}

std::string properties_t::table_name() const
{
    return qualified_name(m_schema, properties_table);
}
