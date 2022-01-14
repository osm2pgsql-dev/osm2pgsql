/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "dependency-manager.hpp"
#include "middle.hpp"

#include <algorithm>
#include <iterator>

void full_dependency_manager_t::node_changed(osmid_t id)
{
    for (auto const way_id : m_object_store->get_ways_by_node(id)) {
        way_changed(way_id);
        m_ways_pending_tracker.set(way_id);
    }

    for (auto const rel_id : m_object_store->get_rels_by_node(id)) {
        m_rels_pending_tracker.set(rel_id);
    }
}

void full_dependency_manager_t::way_changed(osmid_t id)
{
    if (m_ways_pending_tracker.get(id)) {
        return;
    }

    for (auto const rel_id : m_object_store->get_rels_by_way(id)) {
        m_rels_pending_tracker.set(rel_id);
    }
}

bool full_dependency_manager_t::has_pending() const noexcept
{
    return !m_ways_pending_tracker.empty() || !m_rels_pending_tracker.empty();
}

idlist_t full_dependency_manager_t::get_ids(osmium::index::IdSetSmall<osmid_t> &tracker)
{
    tracker.sort_unique();

    idlist_t list;
    list.reserve(tracker.size());

    std::copy(tracker.cbegin(), tracker.cend(), std::back_inserter(list));

    tracker.clear();

    return list;
}
