#ifndef OSM2PGSQL_TAGINFO_HPP
#define OSM2PGSQL_TAGINFO_HPP

#include <string>
#include <vector>

enum class ColumnType
{
    INT,
    REAL,
    TEXT
};

struct Column
{
    Column(std::string const &n, std::string const &tn, ColumnType t)
    : name(n), type_name(tn), type(t)
    {}

    std::string name;
    std::string type_name;
    ColumnType type;
};

using columns_t = std::vector<Column>;

#endif // OSM2PGSQL_TAGINFO_HPP
