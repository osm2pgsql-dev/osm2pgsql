#ifndef PGSQL_ID_TRACKER_HPP
#define PGSQL_ID_TRACKER_HPP

#include "id-tracker.hpp"

struct pgsql_id_tracker : public id_tracker {
    pgsql_id_tracker(const std::string &conninfo, 
                     const std::string &prefix, 
                     const std::string &type,
                     bool owns_table);
    ~pgsql_id_tracker();

    void mark(osmid_t id);
    bool is_marked(osmid_t id);

    osmid_t pop_mark();

    void commit();
    void force_release(); // to avoid brain-damages with fork()

private:
    void unmark(osmid_t id);

    struct pimpl;
    boost::scoped_ptr<pimpl> impl;
};

#endif /* PGSQL_ID_TRACKER_HPP */
