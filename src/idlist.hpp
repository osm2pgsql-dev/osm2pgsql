#ifndef OSM2PGSQL_IDLIST_HPP
#define OSM2PGSQL_IDLIST_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * This file contains the definition of the idlist_t class.
 */

#include "osmtypes.hpp"

#include <vector>

/**
 * A list of OSM object ids. Internally this is a vector of ids.
 *
 * Some operations are only allowed when the list of ids is sorted and
 * without duplicates. Call sort_unique() to achieve this.
 */
class idlist_t
{
public:
    using value_type = osmid_t;

    idlist_t() = default;
    ~idlist_t() noexcept = default;

    idlist_t(std::initializer_list<osmid_t> ids) : m_list(ids) {}

    idlist_t(idlist_t const &) = delete;
    idlist_t &operator=(idlist_t const &) = delete;

    idlist_t(idlist_t &&) = default;
    idlist_t &operator=(idlist_t &&) = default;

    bool empty() const noexcept { return m_list.empty(); }

    std::size_t size() const noexcept { return m_list.size(); }

    auto begin() const noexcept { return m_list.begin(); }

    auto end() const noexcept { return m_list.end(); }

    auto cbegin() const noexcept { return m_list.cbegin(); }

    auto cend() const noexcept { return m_list.cend(); }

    osmid_t operator[](std::size_t n) const noexcept { return m_list[n]; }

    void clear() noexcept { m_list.clear(); }

    void push_back(osmid_t id) { m_list.push_back(id); }

    void reserve(std::size_t size) { m_list.reserve(size); }

    /**
     * Remove id at the end of the list and return it.
     *
     * \pre \code !m_list.empty()) \endcode
     */
    osmid_t pop_id();

    /// List are equal if they contain the same ids in the same order.
    friend bool operator==(idlist_t const &lhs, idlist_t const &rhs) noexcept
    {
        return lhs.m_list == rhs.m_list;
    }

    friend bool operator!=(idlist_t const &lhs, idlist_t const &rhs) noexcept
    {
        return !(lhs == rhs);
    }

    /**
     * Sort this list and remove duplicates.
     */
    void sort_unique();

    /**
     * Merge other list into this one.
     *
     * \pre Both lists must be sorted and without duplicates.
     */
    void merge_sorted(idlist_t const &other);

    /**
     * Remove all ids in this list that are also in the other list.
     *
     * \pre Both lists must be sorted and without duplicates.
     */
    void remove_ids_if_in(idlist_t const &other);

private:
    std::vector<osmid_t> m_list;

}; // class idlist_t

#endif // OSM2PGSQL_IDLIST_HPP
