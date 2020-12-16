#ifndef OSM2PGSQL_PGSQL_HELPER_HPP
#define OSM2PGSQL_PGSQL_HELPER_HPP

#include <string>

#include "pgsql.hpp"

/**
 * Iterate over the result from a pgsql query and generate a list of all the
 * ids from the first column.
 *
 * \param result The result to iterate over.
 * \returns A list of ids.
 */
idlist_t get_ids_from_result(pg_result_t const &result);

void create_geom_check_trigger(pg_conn_t *db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &geom_column);

void analyze_table(pg_conn_t const &db_connection, std::string const &schema,
                   std::string const &name);

#endif // OSM2PGSQL_PGSQL_HELPER_HPP
