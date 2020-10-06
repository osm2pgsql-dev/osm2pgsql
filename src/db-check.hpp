#ifndef OSM2PGSQL_DB_CHECK_HPP
#define OSM2PGSQL_DB_CHECK_HPP

#include "options.hpp"

/**
 * Get settings from the database and check that minimum requirements for
 * osm2pgsql are met. This also prints the database version.
 */
void check_db(options_t const &options);

#endif // OSM2PGSQL_DB_CHECK_HPP
