/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "settings.hpp"

#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"

static constexpr char const *const settings_table = "osm2pgsql_settings";

settings_t::settings_t(std::string const &conninfo, std::string const &schema)
: m_conninfo(conninfo), m_schema(schema),
  m_has_settings_table(
      has_table(schema.empty() ? "public" : schema, settings_table))
{
    log_debug("Found settings table '{}': {}.", settings_table,
              m_has_settings_table);
}

std::string settings_t::get_string(std::string const &option,
                                   std::string const &default_value) const
{
    auto const it = m_settings.find(option);
    if (it == m_settings.end()) {
        return default_value;
    }

    return it->second;
}

int64_t settings_t::get_int(std::string const &option,
                            int64_t default_value) const
{
    auto const it = m_settings.find(option);
    if (it == m_settings.end()) {
        return default_value;
    }

    char *end = nullptr;
    errno = 0;
    int64_t const val = std::strtol(it->second.data(), &end, 10);
    if (errno || *end != '\0') {
        throw fmt_error("Corruption in settings: '{}' must be an integer.",
                        option);
    }

    return val;
}

bool settings_t::get_bool(std::string const &option, bool default_value) const
{
    auto const it = m_settings.find(option);
    if (it == m_settings.end()) {
        return default_value;
    }

    if (it->second == "true") {
        return true;
    }
    if (it->second == "false") {
        return false;
    }

    throw fmt_error("Corruption in settings: '{}' must be 'true' or 'false'.",
                    option);
}

void settings_t::set_string(std::string option, std::string value,
                            bool update_database)
{
    m_settings.insert_or_assign(std::move(option), std::move(value));
    if (update_database) {
        update_setting(option);
    }
}

void settings_t::set_int(std::string option, int64_t value,
                         bool update_database)
{
    m_settings.insert_or_assign(std::move(option), std::to_string(value));
    if (update_database) {
        update_setting(option);
    }
}

void settings_t::set_bool(std::string option, bool value, bool update_database)
{
    m_settings.insert_or_assign(std::move(option), value ? "true" : "false");
    if (update_database) {
        update_setting(option);
    }
}

void settings_t::update_setting(std::string const &option) const
{
    pg_conn_t const db_connection{m_conninfo};

    db_connection.exec("PREPARE setting(text, text) AS"
                       " UPDATE {} SET value = $2 WHERE option = $1",
                       table_name());

    auto const value = m_settings.at(option);
    log_debug("  Storing {}='{}'", option, value);
    db_connection.exec_prepared("setting", option, value);
}

void settings_t::store() const
{
    auto const table = table_name();

    log_info("Storing settings to table '{}'.", table);
    pg_conn_t const db_connection{m_conninfo};

    if (m_has_settings_table) {
        db_connection.exec("TRUNCATE {}", table);
    } else {
        db_connection.exec("CREATE TABLE {} ("
                           " option TEXT NOT NULL PRIMARY KEY,"
                           " value TEXT NOT NULL)",
                           table);
    }

    db_connection.exec("PREPARE setting(text, text) AS"
                       " INSERT INTO {} (option, value) VALUES ($1, $2)",
                       table);

    for (auto const &[k, v] : m_settings) {
        log_debug("  Storing {}='{}'", k, v);
        db_connection.exec_prepared("setting", k, v);
    }
}

bool settings_t::load()
{
    if (!m_has_settings_table) {
        log_info("No settings found in database from previous import.");
        return false;
    }

    m_settings.clear();

    auto const table = table_name();
    log_info("Loading settings from table '{}'.", table);

    pg_conn_t const db_connection{m_conninfo};
    auto const result = db_connection.exec("SELECT * FROM {}", table);

    for (int i = 0; i < result.num_tuples(); ++i) {
        m_settings.insert_or_assign(result.get_value(i, 0), result.get(i, 1));
    }

    return true;
}

std::string settings_t::table_name() const
{
    std::string table;

    if (!m_schema.empty()) {
        table += '"';
        table += m_schema;
        table += '"';
        table += '.';
    }
    table += settings_table;

    return table;
}
