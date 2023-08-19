/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "dependency-manager.hpp"
#include "middle.hpp"

#include <algorithm>
#include <iterator>

void full_dependency_manager_t::node_changed(osmid_t id)
{
    m_changed_nodes.push_back(id);
}

void full_dependency_manager_t::way_changed(osmid_t id)
{
    m_changed_ways.push_back(id);
}

void full_dependency_manager_t::relation_changed(osmid_t id)
{
    m_changed_relations.push_back(id);
}

void full_dependency_manager_t::after_nodes()
{
    if (m_changed_nodes.empty()) {
        return;
    }

    m_object_store->get_node_parents(m_changed_nodes, &m_ways_pending_tracker,
                                     &m_rels_pending_tracker);
    m_changed_nodes.clear();
}

void full_dependency_manager_t::after_ways()
{
    if (!m_changed_ways.empty()) {
        if (!m_ways_pending_tracker.empty()) {
            // Remove ids from changed ways in the input data from
            // m_ways_pending_tracker, because they have already been processed.
            m_ways_pending_tracker.remove_ids_if_in(m_changed_ways);

            // Add the list of pending way ids to the list of changed ways,
            // because we need the parents for them, too.
            m_changed_ways.merge_sorted(m_ways_pending_tracker);
        }

        m_object_store->get_way_parents(m_changed_ways,
                                        &m_rels_pending_tracker);

        m_changed_ways.clear();
        return;
    }

    if (!m_ways_pending_tracker.empty()) {
        m_object_store->get_way_parents(m_ways_pending_tracker,
                                        &m_rels_pending_tracker);
    }
}

void full_dependency_manager_t::after_relations()
{
    // Remove ids from changed relations in the input data from
    // m_rels_pending_tracker, because they have already been processed.
    m_rels_pending_tracker.remove_ids_if_in(m_changed_relations);

    m_changed_relations.clear();
}

void full_dependency_manager_t::mark_parent_relations_as_pending(
    idlist_t const &way_ids)
{
    assert(m_rels_pending_tracker.empty());
    m_object_store->get_way_parents(way_ids, &m_rels_pending_tracker);
}

bool full_dependency_manager_t::has_pending() const noexcept
{
    return !m_ways_pending_tracker.empty() || !m_rels_pending_tracker.empty();
}
