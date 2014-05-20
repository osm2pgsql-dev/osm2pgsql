#include "pgsql-id-tracker.hpp"
#include <set>

struct pgsql_id_tracker::pimpl {
    std::set<osmid_t> ids;
};

pgsql_id_tracker::pgsql_id_tracker() 
    : impl(new pimpl) {
}

pgsql_id_tracker::~pgsql_id_tracker() {
}

void pgsql_id_tracker::done(osmid_t id) {
    impl->ids.insert(id);
}

bool pgsql_id_tracker::is_done(osmid_t id) {
    return impl->ids.count(id) > 0;
}
