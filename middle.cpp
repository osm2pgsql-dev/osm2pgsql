#include "middle.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

#include <boost/make_shared.hpp>

boost::shared_ptr<middle_t> middle_t::create_middle(const bool slim)
{
     if(slim)
         return boost::make_shared<middle_pgsql_t>();
     else
         return boost::make_shared<middle_ram_t>();
}


middle_query_t::~middle_query_t() {
}

middle_t::~middle_t() {
}

slim_middle_t::~slim_middle_t() {
}

middle_t::cb_func::~cb_func() {
}

middle_t::pending_processor::~pending_processor() {
}
