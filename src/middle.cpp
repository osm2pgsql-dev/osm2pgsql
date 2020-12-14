
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "middle.hpp"
#include "options.hpp"

std::shared_ptr<middle_t> create_middle(options_t const &options)
{
    if (options.slim) {
        return std::make_shared<middle_pgsql_t>(&options);
    }

    return std::make_shared<middle_ram_t>(&options);
}

