#ifndef OSM2PGSQL_ID_TRACKER_HPP
#define OSM2PGSQL_ID_TRACKER_HPP

#include "osmtypes.hpp"
#include <memory>

/**
  * Tracker for if an element needs to be revisited later in the process, also
  * known as "pending". This information used to be stored in the database, but
  * is ephemeral and lead to database churn, bloat, and was generally slow.
  * An initial re-implementation stored it as a std::set<osmid_t>, which worked
  * but was inefficient with memory overhead and pointer chasing.
  *
  * Instead, the size of the leaf nodes is increased. This was initially a
  * vector<bool>, but the cost of exposing the iterator was too high.
  * Instead, it's a uint32, with a function to find the next bit set in the block
  *
  * These details aren't exposed in the public interface, which just has
  * pop_mark.
  */
struct id_tracker
{
    id_tracker();

    id_tracker(id_tracker const &) = delete;
    id_tracker &operator=(id_tracker const &) = delete;

    id_tracker(id_tracker &&) = delete;
    id_tracker &operator=(id_tracker &&) = delete;

    ~id_tracker();

    void mark(osmid_t id);
    bool is_marked(osmid_t id);
    /**
     * Finds an osmid_t that is marked
     */
    osmid_t pop_mark();
    size_t size() const;
    bool empty() const;
    osmid_t last_returned() const;

    static bool is_valid(osmid_t);
    static osmid_t max();
    static osmid_t min();

private:
    struct pimpl;
    std::unique_ptr<pimpl> impl;
};

#endif // OSM2PGSQL_ID_TRACKER_HPP
