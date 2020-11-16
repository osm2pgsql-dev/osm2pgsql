#include "db-check.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "version.hpp"

#include <stdexcept>

/**
 * Check whether the table with the specified name exists in the specified
 * schema in the database. Leave schema empty to check in the 'public' schema.
 */
static bool has_table(pg_conn_t &db_connection, std::string const &schema,
                      std::string const &table)
{
    auto const sql = "SELECT count(*) FROM pg_tables"
                     "  WHERE schemaname='{}' AND tablename='{}'"_format(
                         schema.empty() ? "public" : schema, table);
    auto const res = db_connection.query(PGRES_TUPLES_OK, sql);
    auto const num = res.get_value(0, 0);

    return num[0] == '1' && num[1] == '\0';
}

void check_db(options_t const &options)
{
    pg_conn_t db_connection{options.database_options.conninfo()};

    auto const settings = get_postgresql_settings(db_connection);

    try {
        log_info("Database version: {}", settings.at("server_version"));

        auto const version_str = settings.at("server_version_num");
        auto const version = std::strtoul(version_str.c_str(), nullptr, 10);
        if (version < get_minimum_postgresql_server_version_num()) {
            throw std::runtime_error{
                "Your database version is too old (need at least {})."_format(
                    get_minimum_postgresql_server_version())};
        }

        // If we are in append mode and the middle nodes table isn't there,
        // it probably means we used a flat node store when we created this
        // database. Check for that and stop if it looks like we are missing
        // the node location store option.
        if (options.append && !options.flat_node_cache_enabled) {
            if (!has_table(db_connection, options.middle_dbschema,
                           options.prefix + "_nodes")) {
                throw std::runtime_error{
                    "You seem to not have a nodes table. Did "
                    "you forget the --flat-nodes option?"};
            }
        }

    } catch (std::out_of_range const &) {
        // Thrown by the settings.at() if the named setting isn't found
        throw std::runtime_error{"Can't access database setting."};
    }
}
