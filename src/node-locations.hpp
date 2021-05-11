#ifndef OSM2PGSQL_NODE_LOCATIONS_HPP
#define OSM2PGSQL_NODE_LOCATIONS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "ordered-index.hpp"
#include "osmtypes.hpp"

#include <osmium/util/delta.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

/**
 * Node locations storage. This implementation encodes ids and locations
 * with delta encoding and varints making it very memory-efficient but a bit
 * slower than other implementations.
 *
 * Internally nodes are stored in blocks of `block_size` (id, location) pairs.
 * Ids inside a block and the x and y coordinates of each location are first
 * delta encoded and then stored as varints. To access a stored location the
 * block must be decoded until the id is found.
 *
 * Ids must be added in strictly ascending order.
 */
class node_locations_t
{
public:
    /**
     * Store a node location.
     *
     * \pre id must be strictly larger than all ids stored before.
     */
    void set(osmid_t id, osmium::Location location);

    /**
     * Retrieve a node location. If the location wasn't stored before, an
     * invalid Location will be returned.
     */
    osmium::Location get(osmid_t id) const;

    /// The number of locations stored.
    std::size_t size() const noexcept { return m_count; }

    /// Return the approximate number of bytes used for internal storage.
    std::size_t used_memory() const noexcept
    {
        return m_data.capacity() + m_index.used_memory();
    }

    /**
     * Clear the memory used by this object. The object can be reused after
     * that.
     */
    void clear();

private:
    bool first_entry_in_block() const noexcept
    {
        return m_count % block_size == 0;
    }

    /**
     * The block size used for internal blocks. The larger the block size
     * the less memory is consumed but the more expensive the access is.
     */
    static constexpr const std::size_t block_size = 32;

    ordered_index_t m_index;
    std::string m_data;

    /// The number of (id, location) pairs stored.
    std::size_t m_count = 0;

    osmium::DeltaEncode<osmid_t> m_did;
    osmium::DeltaEncode<int64_t> m_dx;
    osmium::DeltaEncode<int64_t> m_dy;
}; // class node_locations_t

#endif // OSM2PGSQL_NODE_LOCATIONS_HPP
