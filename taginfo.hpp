#ifndef TAGINFO_HPP
#define TAGINFO_HPP

#include <string>
#include <vector>

enum ColumnType {
    COLUMN_TYPE_INT,
    COLUMN_TYPE_REAL,
    COLUMN_TYPE_TEXT
};

struct Column
{
    Column(std::string const &n, std::string const &tn, ColumnType t)
        : name(n), type_name(tn), type(t) {}

    std::string name;
    std::string type_name;
    ColumnType type;
};

typedef std::vector<Column> columns_t;

/* Table columns, representing key= tags */
struct taginfo;

/* list of exported tags */
struct export_list;

#endif /* TAGINFO_HPP */
