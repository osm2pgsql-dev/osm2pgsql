#ifndef OSM2PGSQL_DEPENDENCY_MANAGER_HPP
#define OSM2PGSQL_DEPENDENCY_MANAGER_HPP

#include "id-tracker.hpp"
#include "osmtypes.hpp"

#include <cassert>
#include <memory>

struct middle_t;

/**
 * The job of the dependency manager is to keep track of the dependencies
 * between OSM objects, that is nodes in ways and members of relations.
 *
 * Whenever an OSM object changes, this class is notified and can remember
 * the ids for later use.
 *
 * This base class doesn't actually do the dependency management but is a
 * dummy for cases where no dependency management is needed. See the
 * full_dependency_manager_t class for the real dependency manager.
 */
class dependency_manager_t
{
public:
    virtual ~dependency_manager_t() = default;

    /**
     * Mark a node as changed to trigger the propagation of this change to
     * ways and relations.
     *
     * This has to be called *after* the object was stored in the object store.
     */
    virtual void node_changed(osmid_t) {}

    /**
     * Mark a way as changed to trigger the propagation of this change to
     * relations.
     *
     * This has to be called *after* the object was stored in the object store.
     */
    virtual void way_changed(osmid_t) {}

    /// Are there pending objects that need to be processed?
    virtual bool has_pending() const noexcept { return false; }

    /**
     * Get the list of pending way ids. After calling this, the internal
     * list is cleared.
     */
    virtual idlist_t get_pending_way_ids() { return {}; }

    /**
     * Get the list of pending relation ids. After calling this, the internal
     * list is cleared.
     */
    virtual idlist_t get_pending_relation_ids() { return {}; }
};

/**
 * The job of the dependency manager is to keep track of the dependencies
 * between OSM objects, that is nodes in ways and members of relations.
 *
 * Whenever an OSM object changes, this class is notified and remembers
 * the ids for later use.
 */
class full_dependency_manager_t : public dependency_manager_t
{
public:
    /**
     * Constructor.
     *
     * \param object_store Pointer to the middle that keeps the actual
     *        database of objects and their relationsships.
     *
     * \pre object_store != nullptr
     */
    explicit full_dependency_manager_t(std::shared_ptr<middle_t> object_store)
    : m_object_store(std::move(object_store))
    {
        assert(m_object_store != nullptr);
    }

    void node_changed(osmid_t id) override;
    void way_changed(osmid_t id) override;

    bool has_pending() const noexcept override;

    idlist_t get_pending_way_ids() override
    {
        return get_ids(m_ways_pending_tracker);
    }

    idlist_t get_pending_relation_ids() override
    {
        return get_ids(m_rels_pending_tracker);
    }

private:
    static idlist_t get_ids(id_tracker &tracker);

    std::shared_ptr<middle_t> m_object_store;

    id_tracker m_ways_pending_tracker;
    id_tracker m_rels_pending_tracker;
};

#endif // OSM2PGSQL_DEPENDENCY_MANAGER_HPP
