#ifndef OSM2PGSQL_VERSION_HPP
#define OSM2PGSQL_VERSION_HPP

char const *get_osm2pgsql_version() noexcept;
char const *get_osm2pgsql_short_version() noexcept;
char const *get_minimum_postgresql_server_version() noexcept;
unsigned long get_minimum_postgresql_server_version_num() noexcept;

#endif // OSM2PGSQL_VERSION_HPP
