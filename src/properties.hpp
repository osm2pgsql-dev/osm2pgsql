#ifndef OSM2PGSQL_PROPERTIES_HPP
#define OSM2PGSQL_PROPERTIES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Osm2pgsql stores some properties in the database. Properties come from
 * command line options or from the input files, they are used to keep the
 * configuration consistent between imports and updates.
 */

#include "pgsql-params.hpp"

#include <cstdint>
#include <map>
#include <string>

class pg_conn_t;

class properties_t
{
public:
    /**
     * Create new properties store.
     *
     * \param connection_params Parameters used to connect to the database.
     * \param schema The schema used for storing the data,
     *
     * \pre You must have called init_database_capabilities() before this.
     */
    properties_t(connection_params_t connection_params, std::string schema);

    std::size_t size() const noexcept { return m_properties.size(); }

    std::string get_string(std::string const &property,
                           std::string const &default_value) const;

    int64_t get_int(std::string const &property, int64_t default_value) const;

    bool get_bool(std::string const &property, bool default_value) const;

    /**
     * Set property to string value.
     *
     * \param property Name of the property
     * \param value Value of the property
     */
    void set_string(std::string const &property, std::string const &value);

    /**
     * Set property to integer value. The integer will be converted to a string
     * internally.
     *
     * \param property Name of the property
     * \param value Value of the property
     */
    void set_int(std::string const &property, int64_t value);

    /**
     * Set property to boolean value. In the database this will show up as the
     * string 'true' or 'false'.
     *
     * \param property Name of the property
     * \param value Value of the property
     */
    void set_bool(std::string const &property, bool value);

    /**
     * Initialize the database table 'osm2pgsql_properties'. It is created if
     * it does not exist and truncated.
     */
    void init_table();

    /**
     * Store all properties in the database that changed since the last store.
     * Overwrites any properties that might already be stored in the database.
     */
    void store();

    /**
     * Load all properties from the database. Clears any properties that might
     * already exist before loading.
     *
     * \returns true if properties could be loaded, false otherwise.
     */
    bool load();

    auto begin() const { return m_properties.begin(); }

    auto end() const { return m_properties.end(); }

private:
    std::string table_name() const;

    // The properties
    std::map<std::string, std::string> m_properties;

    // Temporary storage of all properties that need to be updated in the
    // database.
    std::map<std::string, std::string> m_to_update;

    connection_params_t m_connection_params;
    std::string m_schema;
    bool m_has_properties_table;

}; // class properties_t

#endif // OSM2PGSQL_PROPERTIES_HPP
