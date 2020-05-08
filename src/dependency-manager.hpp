#ifndef OSM2PGSQL_DEPENDENCY_MANAGER_HPP
#define OSM2PGSQL_DEPENDENCY_MANAGER_HPP

#include "id-tracker.hpp"
#include "middle.hpp"
#include "osmtypes.hpp"

#include <cassert>

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
    virtual void node_changed(osmid_t) {};

    /**
     * Mark a way as changed to trigger the propagation of this change to
     * relations.
     *
     * This has to be called *after* the object was stored in the object store.
     */
    virtual void way_changed(osmid_t) {};

    /**
     * Mark a relation as changed to trigger the propagation of this change to
     * other relations.
     *
     * This has to be called *after* the object was stored in the object store.
     */
    virtual void relation_changed(osmid_t) {};

    /**
     * Mark a relation as deleted to trigger the propagation of this change to
     * the way members.
     */
    virtual void relation_deleted(osmid_t) {};

    /// Are there pending objects that need to be processed?
    virtual bool has_pending() const noexcept { return false; }

    /**
     * Process all pending objects.
     *
     * \param pf Processor that we should feed the objects to and that
     *        will handle the actual processing.
     *
     * \post !has_pending()
     */
    virtual void process_pending(middle_t::pending_processor &) {}
};

/**
 * The job of the dependency manager is to keep track of the dependencies
 * between OSM objects, that is nodes in ways and members of relations.
 *
 * Whenever an OSM object changes, this class is notified and remembers
 * the ids for later use. Later on the class can be told to process the
 * ids it has remembered.
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
    explicit full_dependency_manager_t(middle_t *object_store)
    : m_object_store(object_store)
    {
        assert(object_store != nullptr);
    }

    void node_changed(osmid_t id) override;
    void way_changed(osmid_t id) override;
    void relation_changed(osmid_t id) override;
    void relation_deleted(osmid_t id) override;

    bool has_pending() const noexcept override;

    void process_pending(middle_t::pending_processor &pf) override;

private:
    middle_t* m_object_store;

    id_tracker m_ways_pending_tracker;
    id_tracker m_rels_pending_tracker;
};

#endif // OSM2PGSQL_DEPENDENCY_MANAGER_HPP
