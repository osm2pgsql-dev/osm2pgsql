#include <iostream>
#include "pgsql.hpp"

#include <iostream>
void test_escape(const char *in, const char *out) {
    std::string sql;
    escape(in, sql);
    if (sql.compare(out) != 0) {
        std::cerr << "Expected " << out << ", but got " << sql << " for " << in <<".\n";
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    std::string sql;
    test_escape("farmland", "farmland");
    test_escape("", "");
    test_escape("\\", "\\\\");
    test_escape("foo\nbar", "foo\\\nbar");
    test_escape("\t\r\n", "\\\t\\\r\\\n");

    return 0;
}
