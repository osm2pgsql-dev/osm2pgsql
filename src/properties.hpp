#ifndef OSM2PGSQL_PROPERTIES_HPP
#define OSM2PGSQL_PROPERTIES_HPP

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
 * Osm2pgsql stores some properties in the database. Properties come from
 * command line options or from the input files, they are used to keep the
 * configuration consistent between imports and updates.
 */

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
     * \param conninfo Connection info used to connect to the database.
     * \param schema The schema used for storing the data,
     *
     * \pre You must have called init_database_capabilities() before this.
     */
    properties_t(std::string const &conninfo, std::string const &schema);

    std::string get_string(std::string const &property,
                           std::string const &default_value) const;

    int64_t get_int(std::string const &property, int64_t default_value) const;

    bool get_bool(std::string const &property, bool default_value) const;

    /**
     * Set property to string value.
     *
     * \param property Name of the property
     * \param value Value of the property
     * \param update_database Update database with this value immediately.
     */
    void set_string(std::string property, std::string value,
                    bool update_database = false);

    /**
     * Set property to integer value. The integer will be converted to a string
     * internally.
     *
     * \param property Name of the property
     * \param value Value of the property
     * \param update_database Update database with this value immediately.
     */
    void set_int(std::string property, int64_t value,
                 bool update_database = false);

    /**
     * Set property to boolean value. In the database this will show up as the
     * string 'true' or 'false'.
     *
     * \param property Name of the property
     * \param value Value of the property
     * \param update_database Update database with this value immediately.
     */
    void set_bool(std::string property, bool value,
                  bool update_database = false);

    /**
     * Store all properties in the database. Creates the properties table in
     * the database if needed. Removes any properties that might already be
     * stored in the database.
     */
    void store();

    /**
     * Load all properties from the database. Clears any properties that might
     * already exist before loading.
     *
     * \returns true if properties could be loaded, false otherwise.
     */
    bool load();

private:
    std::string table_name() const;

    void update(std::string const &property) const;

    std::map<std::string, std::string> m_properties;
    std::string m_conninfo;
    std::string m_schema;
    bool m_has_properties_table;

}; // class properties_t

#endif // OSM2PGSQL_PROPERTIES_HPP
