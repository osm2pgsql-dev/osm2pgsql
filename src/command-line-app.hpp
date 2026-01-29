#ifndef OSM2PGSQL_COMMAND_LINE_APP_HPP
#define OSM2PGSQL_COMMAND_LINE_APP_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "options.hpp"

#include <CLI/CLI.hpp>

#include <string>
#include <utility>

class command_line_app_t : public CLI::App
{
public:
    explicit command_line_app_t(std::string app_description);

    bool want_help() const;

    bool want_version() const;

    connection_params_t connection_params() const noexcept
    {
        return m_connection_params;
    }

    void init_database_options();
    void init_logging_options(bool with_progress, bool with_sql);

private:
    connection_params_t m_connection_params;

}; // class App

#endif // OSM2PGSQL_COMMAND_LINE_APP_HPP
