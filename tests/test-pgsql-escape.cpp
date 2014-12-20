#include "pgsql.hpp"

int main(int argc, char *argv[]) {
    std::string sql;
    escape("farmland", sql);
    return sql.compare("farmland") != 0;
}
