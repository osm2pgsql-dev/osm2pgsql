#ifndef OSM2PGSQL_DEPENDENCY_MANAGER_HPP
#define OSM2PGSQL_DEPENDENCY_MANAGER_HPP

#include "id-tracker.hpp"
#include "middle.hpp"
#include "osmtypes.hpp"

#include <cassert>

struct pending_processor
{
    virtual ~pending_processor() = default;

    virtual void enqueue_way(osmid_t id) = 0;
    virtual void enqueue_relation(osmid_t id) = 0;

    virtual void process_ways() = 0;
    virtual void process_relations() = 0;
};

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
    virtual void process_pending(pending_processor &) {}
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

    void process_pending(pending_processor &proc) override;

    /**
     * Get access to the pending way ids. This is for debugging only.
     *
     * Note that the list of pending way ids will be empty after calling
     * this.
     *
     * \tparam TOutputIterator Some output iterator type, for instance
     *         created with std::back_inserter(some vector).
     * \param it output iterator to which all ids should be written. *it
     *        must be of type osmid_t.
     */
    template <typename TOutputIterator>
    void get_pending_way_ids(TOutputIterator &&it) {
        osmid_t id;
        while (id_tracker::is_valid(id = m_ways_pending_tracker.pop_mark())) {
            *it++ = id;
        }
    }

    /**
     * Get access to the pending relation ids. This is for debugging only.
     *
     * Note that the list of pending relation ids will be empty after calling
     * this.
     *
     * \tparam TOutputIterator Some output iterator type, for instance
     *         created with std::back_inserter(some vector).
     * \param it output iterator to which all ids should be written. *it
     *        must be of type osmid_t.
     */
    template <typename TOutputIterator>
    void get_pending_relation_ids(TOutputIterator &&it) {
        osmid_t id;
        while (id_tracker::is_valid(id = m_rels_pending_tracker.pop_mark())) {
            *it++ = id;
        }
    }

private:
    middle_t* m_object_store;

    id_tracker m_ways_pending_tracker;
    id_tracker m_rels_pending_tracker;
};

#endif // OSM2PGSQL_DEPENDENCY_MANAGER_HPP
