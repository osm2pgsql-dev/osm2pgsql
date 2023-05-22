/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "properties.hpp"

#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"

#include <cstdlib>

static constexpr char const *const properties_table = "osm2pgsql_properties";

properties_t::properties_t(std::string const &conninfo,
                           std::string const &schema)
: m_conninfo(conninfo), m_schema(schema),
  m_has_properties_table(
      has_table(schema.empty() ? "public" : schema, properties_table))
{
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

void properties_t::set_string(std::string property, std::string value,
                              bool update_database)
{
    auto const r =
        m_properties.insert_or_assign(std::move(property), std::move(value));

    if (!update_database || !m_has_properties_table) {
        return;
    }

    auto const &inserted = *(r.first);
    log_debug("  Storing {}='{}'", inserted.first, inserted.second);

    pg_conn_t const db_connection{m_conninfo};
    db_connection.exec(
        "PREPARE set_property(text, text) AS"
        " INSERT INTO {} (property, value) VALUES ($1, $2)"
        " ON CONFLICT (property) DO UPDATE SET value = EXCLUDED.value",
        table_name());
    db_connection.exec_prepared("set_property", inserted.first,
                                inserted.second);
}

void properties_t::set_int(std::string property, int64_t value,
                           bool update_database)
{
    set_string(std::move(property), std::to_string(value), update_database);
}

void properties_t::set_bool(std::string property, bool value,
                            bool update_database)
{
    set_string(std::move(property), value ? "true" : "false", update_database);
}

void properties_t::store()
{
    auto const table = table_name();

    log_info("Storing properties to table '{}'.", table);
    pg_conn_t const db_connection{m_conninfo};

    if (m_has_properties_table) {
        db_connection.exec("TRUNCATE {}", table);
    } else {
        db_connection.exec("CREATE TABLE {} ("
                           " property TEXT NOT NULL PRIMARY KEY,"
                           " value TEXT NOT NULL)",
                           table);
        m_has_properties_table = true;
    }

    db_connection.exec("PREPARE set_property(text, text) AS"
                       " INSERT INTO {} (property, value) VALUES ($1, $2)",
                       table);

    for (auto const &[k, v] : m_properties) {
        log_debug("  Storing {}='{}'", k, v);
        db_connection.exec_prepared("set_property", k, v);
    }
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

    pg_conn_t const db_connection{m_conninfo};
    auto const result = db_connection.exec("SELECT * FROM {}", table);

    for (int i = 0; i < result.num_tuples(); ++i) {
        m_properties.insert_or_assign(result.get_value(i, 0), result.get(i, 1));
    }

    return true;
}

std::string properties_t::table_name() const
{
    std::string table;

    if (!m_schema.empty()) {
        table += '"';
        table += m_schema;
        table += '"';
        table += '.';
    }
    table += properties_table;

    return table;
}
