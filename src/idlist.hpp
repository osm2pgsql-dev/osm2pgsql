#ifndef OSM2PGSQL_IDLIST_HPP
#define OSM2PGSQL_IDLIST_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * This file contains the definition of the idlist_t class.
 */

#include "osmtypes.hpp"

#include <cassert>
#include <vector>

class idlist_t
{
public:
    using value_type = osmid_t;

    idlist_t() = default;

    idlist_t(std::initializer_list<osmid_t> ids) : m_list(ids) {}

    bool empty() const noexcept { return m_list.empty(); }

    std::size_t size() const noexcept { return m_list.size(); }

    auto begin() const noexcept { return m_list.begin(); }

    auto end() const noexcept { return m_list.end(); }

    osmid_t operator[](std::size_t n) const noexcept { return m_list[n]; }

    void clear() { m_list.clear(); }

    void push_back(osmid_t id) { m_list.push_back(id); }

    void reserve(std::size_t size) { m_list.reserve(size); }

    osmid_t pop_id()
    {
        assert(!m_list.empty());
        auto const id = m_list.back();
        m_list.pop_back();
        return id;
    }

    friend bool operator==(idlist_t const &lhs, idlist_t const &rhs) noexcept
    {
        return lhs.m_list == rhs.m_list;
    }

    friend bool operator!=(idlist_t const &lhs, idlist_t const &rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    std::vector<osmid_t> m_list;

}; // class idlist_t

#endif // OSM2PGSQL_IDLIST_HPP
