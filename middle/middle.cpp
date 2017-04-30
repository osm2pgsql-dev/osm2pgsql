#include "middle.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

#include <memory>

std::shared_ptr<middle_t> middle_t::create_middle(const bool slim)
{
     if(slim)
         return std::make_shared<middle_pgsql_t>();
     else
         return std::make_shared<middle_ram_t>();
}

