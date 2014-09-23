#include "middle.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

middle_t* middle_t::create_middle(const bool slim)
{
     return slim ? (middle_t*)new middle_pgsql_t() : (middle_t*)new middle_ram_t();
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
