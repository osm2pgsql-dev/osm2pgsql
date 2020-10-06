#include "db-check.hpp"
#include "format.hpp"
#include "pgsql.hpp"
#include "version.hpp"

#include <stdexcept>

void check_db(options_t const &options)
{
    pg_conn_t db_connection{options.database_options.conninfo()};

    auto const settings = get_postgresql_settings(db_connection);

    try {
        fmt::print("Database version: {}\n", settings.at("server_version"));

        auto const version_str = settings.at("server_version_num");
        auto const version = std::strtoul(version_str.c_str(), nullptr, 10);
        if (version < get_minimum_postgresql_server_version_num()) {
            throw std::runtime_error{
                "Your database version is too old (need at least {})."_format(
                    get_minimum_postgresql_server_version())};
        }

    } catch (std::out_of_range const &) {
        // Thrown by the settings.at() if the named setting isn't found
        throw std::runtime_error{"Can't access database setting."};
    }
}
