#include "id-tracker.hpp"


#include <set>
#include <limits>

struct id_tracker::pimpl {
    pimpl();
    ~pimpl();

    std::set<osmid_t> pending;
    osmid_t old_id;
};

id_tracker::pimpl::pimpl(): old_id(0) {
}

id_tracker::pimpl::~pimpl() {
}

id_tracker::id_tracker(): impl() {
    impl.reset(new pimpl());
}

id_tracker::~id_tracker() {
}

void id_tracker::mark(osmid_t id) {
    impl->pending.insert(id);
}

bool id_tracker::is_marked(osmid_t id) {
    return impl->pending.find(id) != impl->pending.end();
}

osmid_t id_tracker::pop_mark() {
    osmid_t id = std::numeric_limits<osmid_t>::max();

    //if we have any get the first one and remove it
    if (impl->pending.size()) {
        id = *impl->pending.begin();
        impl->pending.erase(impl->pending.begin());
    }

    assert((id > impl->old_id) || (id == std::numeric_limits<osmid_t>::max()));
    impl->old_id = id;

    return id;
}

void id_tracker::unmark(osmid_t id) {
    impl->pending.erase(id);
}

void id_tracker::commit() {

}

void id_tracker::force_release() {

}
