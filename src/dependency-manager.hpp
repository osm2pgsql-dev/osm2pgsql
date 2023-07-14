#ifndef OSM2PGSQL_DEPENDENCY_MANAGER_HPP
#define OSM2PGSQL_DEPENDENCY_MANAGER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "osmtypes.hpp"

#include <osmium/index/id_set.hpp>

#include <cassert>
#include <memory>
#include <utility>

class middle_t;

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

    virtual void relation_changed(osmid_t) {}

    virtual void after_nodes() {}
    virtual void after_ways() {}
    virtual void after_relations() {}

    virtual void mark_parent_relations_as_pending(
        osmium::index::IdSetSmall<osmid_t> const & /*way_ids*/)
    {
    }

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
    void relation_changed(osmid_t id) override;

    void after_nodes() override;
    void after_ways() override;
    void after_relations() override;

    void mark_parent_relations_as_pending(
        osmium::index::IdSetSmall<osmid_t> const &ids) override;

    bool has_pending() const noexcept override;

    idlist_t get_pending_way_ids() override
    {
        return get_ids(&m_ways_pending_tracker);
    }

    idlist_t get_pending_relation_ids() override
    {
        return get_ids(&m_rels_pending_tracker);
    }

private:
    static idlist_t get_ids(osmium::index::IdSetSmall<osmid_t> *tracker);

    std::shared_ptr<middle_t> m_object_store;

    /**
     * In append mode all new and changed nodes will be added to this. After
     * all nodes are read this is used to figure out which parent ways and
     * relations reference these nodes. Deleted nodes are not stored in here,
     * because all ways and relations that referenced deleted nodes must be in
     * the change file, too, and so we don't have to find out which ones they
     * are.
     */
    osmium::index::IdSetSmall<osmid_t> m_changed_nodes;

    /**
     * In append mode all new and changed ways will be added to this. After
     * all ways are read this is used to figure out which parent relations
     * reference these ways. Deleted ways are not stored in here, because all
     * relations that referenced deleted ways must be in the change file, too,
     * and so we don't have to find out which ones they are.
     */
    osmium::index::IdSetSmall<osmid_t> m_changed_ways;

    /**
     * In append mode all new and changed relations will be added to this.
     * This is then used to remove already processed relations from the
     * pending list.
     */
    osmium::index::IdSetSmall<osmid_t> m_changed_relations;

    osmium::index::IdSetSmall<osmid_t> m_ways_pending_tracker;
    osmium::index::IdSetSmall<osmid_t> m_rels_pending_tracker;
};

#endif // OSM2PGSQL_DEPENDENCY_MANAGER_HPP
