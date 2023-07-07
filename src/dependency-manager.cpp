/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "dependency-manager.hpp"
#include "middle.hpp"

#include <algorithm>
#include <iterator>

void full_dependency_manager_t::node_changed(osmid_t id)
{
    m_changed_nodes.set(id);
}

void full_dependency_manager_t::way_changed(osmid_t id)
{
    m_changed_ways.set(id);
}

void full_dependency_manager_t::relation_changed(osmid_t id)
{
    m_changed_relations.set(id);
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

static osmium::index::IdSetSmall<osmid_t>
set_diff(osmium::index::IdSetSmall<osmid_t> const &set,
         osmium::index::IdSetSmall<osmid_t> const &to_be_removed)
{
    osmium::index::IdSetSmall<osmid_t> new_set;

    for (auto const id : set) {
        if (!to_be_removed.get_binary_search(id)) {
            new_set.set(id);
        }
    }

    return new_set;
}

void full_dependency_manager_t::after_ways()
{
    if (!m_changed_ways.empty()) {
        if (!m_ways_pending_tracker.empty()) {
            // Remove ids from changed ways in the input data from
            // m_ways_pending_tracker, because they have already been processed.
            m_ways_pending_tracker =
                set_diff(m_ways_pending_tracker, m_changed_ways);

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
    m_rels_pending_tracker =
        set_diff(m_rels_pending_tracker, m_changed_relations);

    m_changed_relations.clear();
}

void full_dependency_manager_t::mark_parent_relations_as_pending(
    osmium::index::IdSetSmall<osmid_t> const &way_ids)
{
    assert(m_rels_pending_tracker.empty());
    m_object_store->get_way_parents(way_ids, &m_rels_pending_tracker);
}

bool full_dependency_manager_t::has_pending() const noexcept
{
    return !m_ways_pending_tracker.empty() || !m_rels_pending_tracker.empty();
}

idlist_t
full_dependency_manager_t::get_ids(osmium::index::IdSetSmall<osmid_t> *tracker)
{
    tracker->sort_unique();

    idlist_t list;
    list.reserve(tracker->size());

    std::copy(tracker->cbegin(), tracker->cend(), std::back_inserter(list));

    tracker->clear();

    return list;
}
