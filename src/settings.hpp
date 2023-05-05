#ifndef OSM2PGSQL_SETTINGS_HPP
#define OSM2PGSQL_SETTINGS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Osm2pgsql settings (typically from command line) that will be stored in the
 * database so that updates use the same settings.
 */

#include <map>
#include <string>
#include <variant>

class pg_conn_t;

class settings_t
{
public:
    /**
     * Create new settings store.
     *
     * \param conninfo Connection info used to connect to the database.
     * \param schema The schema used for storing the data,
     *
     * \pre You must have called init_database_capabilities() before this.
     */
    settings_t(std::string const &conninfo, std::string const &schema);

    std::string get_string(std::string const &option,
                           std::string const &default_value) const;

    int64_t get_int(std::string const &option, int64_t default_value) const;

    bool get_bool(std::string const &option, bool default_value) const;

    /**
     * Set option to string value.
     *
     * \param option Name of the option to set
     * \param value Setting value
     * \param update Update database with this value immediately.
     */
    void set_string(std::string option, std::string value,
                    bool update_database = false);

    /**
     * Set option to integer value. The integer will be converted to a string
     * internally.
     *
     * \param option Name of the option to set
     * \param value Setting value
     * \param update Update database with this value immediately.
     */
    void set_int(std::string option, int64_t value,
                 bool update_database = false);

    /**
     * Set option to boolean value. In the database this will show up as the
     * string 'true' or 'false'.
     *
     * \param option Name of the option to set
     * \param value Setting value
     * \param update Update database with this value immediately.
     */
    void set_bool(std::string option, bool value, bool update_database = false);

    /**
     * Store all settings in the database. Creates the settings table in the
     * database if needed. Removes any settings that might already be stored
     * in the database.
     */
    void store() const;

    /**
     * Load all settings from the database. Clears any settings that might
     * exist already before loading.
     *
     * \returns true if settings could be loaded, false otherwise.
     */
    bool load();

private:
    std::string table_name() const;

    void update_setting(std::string const &option) const;

    std::map<std::string, std::string> m_settings;
    std::string m_conninfo;
    std::string m_schema;
    bool m_has_settings_table;

}; // class settings_t

#endif // OSM2PGSQL_SETTINGS_HPP
