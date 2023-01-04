/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql-capabilities-int.hpp"
#include "pgsql.hpp"
#include "version.hpp"

#include <stdexcept>

static database_capabilities_t &capabilities() noexcept
{
    static database_capabilities_t c;
    return c;
}

database_capabilities_t &database_capabilities_for_testing() noexcept
{
    return capabilities();
}

static void init_set_from_query(std::set<std::string> *set,
                                pg_conn_t const &db_connection,
                                char const *table, char const *column,
                                char const *condition = "true")
{
    auto const res = db_connection.exec(
        "SELECT {} FROM {} WHERE {}"_format(column, table, condition));
    for (int i = 0; i < res.num_tuples(); ++i) {
        set->emplace(res.get(i, 0));
    }
}

/// Get all config settings from the database.
static void init_settings(pg_conn_t const &db_connection)
{
    auto const res =
        db_connection.exec("SELECT name, setting FROM pg_settings");

    for (int i = 0; i < res.num_tuples(); ++i) {
        capabilities().settings.emplace(res.get(i, 0), res.get(i, 1));
    }
}

static void init_database_name(pg_conn_t const &db_connection)
{
    auto const res = db_connection.exec("SELECT current_catalog");

    if (res.num_tuples() != 1) {
        throw std::runtime_error{
            "Database error: Can not access database name."};
    }

    capabilities().database_name = res.get(0, 0);
}

static void init_postgis_version(pg_conn_t const &db_connection)
{
    auto const res = db_connection.exec(
        "SELECT regexp_split_to_table(extversion, '\\.') FROM"
        " pg_extension WHERE extname='postgis'");

    if (res.num_tuples() == 0) {
        throw fmt_error(
            "The postgis extension is not enabled on the database '{}'."
            " Are you using the correct database?"
            " Enable with 'CREATE EXTENSION postgis;'",
            capabilities().database_name);
    }

    capabilities().postgis = {std::stoi(std::string{res.get(0, 0)}),
                              std::stoi(std::string{res.get(1, 0)})};
}

void init_database_capabilities(pg_conn_t const &db_connection)
{
    init_settings(db_connection);
    init_database_name(db_connection);
    init_postgis_version(db_connection);

    try {
        log_info("Database version: {}",
                 capabilities().settings.at("server_version"));
        log_info("PostGIS version: {}.{}", capabilities().postgis.major,
                 capabilities().postgis.minor);

        auto const version_str =
            capabilities().settings.at("server_version_num");
        capabilities().database_version =
            std::strtoul(version_str.c_str(), nullptr, 10);
        if (capabilities().database_version <
            get_minimum_postgresql_server_version_num()) {
            throw fmt_error(
                "Your database version is too old (need at least {}).",
                get_minimum_postgresql_server_version());
        }

        if (capabilities().settings.at("server_encoding") != "UTF8") {
            throw std::runtime_error{"Database is not using UTF8 encoding."};
        }

    } catch (std::out_of_range const &) {
        // Thrown by the settings.at() if the named setting isn't found
        throw std::runtime_error{"Can't access database setting."};
    }

    init_set_from_query(&capabilities().extensions, db_connection,
                        "pg_catalog.pg_extension", "extname");
    init_set_from_query(
        &capabilities().schemas, db_connection, "pg_catalog.pg_namespace",
        "nspname", "nspname !~ '^pg_' AND nspname <> 'information_schema'");
    init_set_from_query(&capabilities().tablespaces, db_connection,
                        "pg_catalog.pg_tablespace", "spcname",
                        "spcname != 'pg_global'");
    init_set_from_query(&capabilities().index_methods, db_connection,
                        "pg_catalog.pg_am", "amname", "amtype = 'i'");
}

bool has_extension(std::string const &value)
{
    return capabilities().extensions.count(value);
}

bool has_schema(std::string const &value)
{
    if (value.empty()) {
        return true;
    }
    return capabilities().schemas.count(value);
}

bool has_tablespace(std::string const &value)
{
    if (value.empty()) {
        return true;
    }
    return capabilities().tablespaces.count(value);
}

bool has_index_method(std::string const &value)
{
    return capabilities().index_methods.count(value);
}

uint32_t get_database_version() noexcept
{
    return capabilities().database_version;
}

postgis_version get_postgis_version() noexcept
{
    return capabilities().postgis;
}
