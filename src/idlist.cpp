/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "idlist.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

osmid_t idlist_t::pop_id()
{
    assert(!m_list.empty());
    auto const id = m_list.back();
    m_list.pop_back();
    return id;
}

void idlist_t::sort_unique()
{
    std::sort(m_list.begin(), m_list.end());
    auto const last = std::unique(m_list.begin(), m_list.end());
    m_list.erase(last, m_list.end());
}

void idlist_t::merge_sorted(idlist_t const &other)
{
    std::vector<osmid_t> new_list;

    new_list.reserve(m_list.size() + other.m_list.size());
    std::set_union(m_list.cbegin(), m_list.cend(), other.m_list.cbegin(),
                   other.m_list.cend(), std::back_inserter(new_list));

    using std::swap;
    swap(new_list, m_list);
}

void idlist_t::remove_ids_if_in(idlist_t const &other)
{
    std::vector<osmid_t> new_list;

    new_list.reserve(m_list.size());
    std::set_difference(m_list.cbegin(), m_list.cend(), other.m_list.cbegin(),
                        other.m_list.cend(), std::back_inserter(new_list));

    using std::swap;
    swap(new_list, m_list);
}
