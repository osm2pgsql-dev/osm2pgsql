#ifndef PGSQL_ID_TRACKER_HPP
#define PGSQL_ID_TRACKER_HPP

#include "osmtypes.hpp"
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

struct pgsql_id_tracker : public boost::noncopyable {
    pgsql_id_tracker();
    ~pgsql_id_tracker();

    void done(osmid_t id);
    bool is_done(osmid_t id);

private:
    struct pimpl;
    boost::scoped_ptr<pimpl> impl;
};

#endif /* PGSQL_ID_TRACKER_HPP */
