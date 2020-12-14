#ifndef OSM2PGSQL_PGSQL_HELPER_HPP
#define OSM2PGSQL_PGSQL_HELPER_HPP

#include <string>

#include "pgsql.hpp"

void create_geom_check_trigger(pg_conn_t *db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &geom_column);

void analyze_table(pg_conn_t const &db_connection, std::string const &schema,
                   std::string const &name);

#endif // OSM2PGSQL_PGSQL_HELPER_HPP
