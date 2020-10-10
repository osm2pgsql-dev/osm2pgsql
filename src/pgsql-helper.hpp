#ifndef OSM2PGSQL_PGSQL_HELPER_HPP
#define OSM2PGSQL_PGSQL_HELPER_HPP

#include "pgsql.hpp"

void create_geom_check_trigger(pg_conn_t *db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &geom_column);

#endif // OSM2PGSQL_PGSQL_HELPER_HPP
