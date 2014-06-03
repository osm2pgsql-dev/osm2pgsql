#include <libpq-fe.h>
#include <cstring>
#include <cstdlib>

#include "pgsql.hpp"
#include "buffer.hpp"

int main(int argc, char *argv[]) {

    std::string escaped = escape("farmland");
    return escaped.compare("farmland") != 0;
}
