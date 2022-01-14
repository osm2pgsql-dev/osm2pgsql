#ifndef OSM2PGSQL_ORDERED_INDEX_HPP
#define OSM2PGSQL_ORDERED_INDEX_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "osmtypes.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

/**
 * This class implements a memory-efficient ordered index for lookups from OSM
 * ids to an "offset" into some kind of primary datastore. Adding to the index
 * is amortized O(1), reading is O(1).
 *
 * Entries must always be added in order from lowest OSM id to highest OSM
 * id and lowest offset to highest offset, ie. both id and offset for each
 * entry must be strictly larger than the previous one. Entries can never
 * be changed.
 *
 * An index that is never used doesn't need more memory than
 * sizeof(ordered_index_t).
 *
 * All allocated memory can be freed by calling clear(). After that the index
 * can NOT be reused.
 *
 * There are two ways of accessing the data through the index:
 * * The get() method returns the offset for the specified id.
 * * The get_block() method returns the offset for the next smaller id, if the
 *   id itself is not found.
 *
 * The implementation is in two levels, the second level blocks contain the id
 * and offset, the first level keeps track of second level blocks and the first
 * and last ids used in each block. There are two reasons for the choice of
 * this two-level design over a simpler vector-based design:
 *
 * * Vectors temporarily use a lot of memory when resizing. We can avoid this
 *   by not resizing the second level blocks. We also save the memcpy needed
 *   when resizing.
 * * To conserve memory, the id and offset in the second level blocks are 32
 *   bit unsigned integers relative to the id and offset of the first id of a
 *   block which is stored in the first level entry. Compared to the 64 bit
 *   integers we would need without the two-level design, this halfs the
 *   memory use.
 */
class ordered_index_t
{
public:
    /**
     * Constructor.
     *
     * \param initial_block_size Number of entries in the initial second level
     *                           index block. Subsequent blocks will each
     *                           double their size until max_block_size is
     *                           reached.
     */
    explicit ordered_index_t(std::size_t initial_block_size = 1024 * 1024)
    : m_block_size(initial_block_size)
    {}

    /**
     * This is the value returned from the getter functions if the id is not
     * in the database.
     */
    static constexpr std::size_t not_found_value() noexcept
    {
        return std::numeric_limits<std::size_t>::max();
    }

    /**
     * How many entries will fit into the currently allocated memory. This
     * is accurate for normal operations, but if there are huge gaps between
     * consecutive ids (> 2^32), less entries than this will fit.
     */
    std::size_t capacity() const noexcept { return m_capacity; }

    /// The number of entries in the index.
    std::size_t size() const noexcept { return m_size; }

    /**
     * Add an entry to the index.
     *
     * \param id The key of the index.
     * \param offset The value of the index.
     *
     * \pre Id and offset must be larger than any previously added id and
     *      offset, respectively.
     */
    void add(osmid_t id, std::size_t offset);

    /**
     * Get the offset for the specified id.
     *
     * If the id is not in the index \code not_found_value() \endcode is
     * returned.
     *
     * \param id The id to look for.
     */
    std::size_t get(osmid_t id) const noexcept
    {
        auto const p = get_internal(id);
        if (p.first != id) {
            return not_found_value();
        }
        return p.second;
    }

    /**
     * Get the offset for the specified id or, if the id is not in the index,
     * the next smaller id available in the index.
     *
     * If the id is not in the index and no smaller id is in the index,
     * \code not_found_value() \endcode is returned.
     *
     * \param id The id to look for.
     */
    std::size_t get_block(osmid_t id) const noexcept
    {
        return get_internal(id).second;
    }

    /**
     * The approximate amount of bytes currently allocated by this index.
     */
    std::size_t used_memory() const noexcept
    {
        return m_ranges.capacity() * sizeof(range_entry) +
               m_capacity * sizeof(second_level_index_entry);
    }

    /**
     * Clear all memory used by this index. The index can NOT be reused after
     * that.
     */
    void clear()
    {
        m_ranges.clear();
        m_ranges.shrink_to_fit();
        m_capacity = 0;
        m_size = 0;
    }

    /// Return true if adding an entry to the index will make it resize.
    bool will_resize() const noexcept { return m_size + 1 >= m_capacity; }

private:
    struct second_level_index_entry
    {
        uint32_t id;
        uint32_t offset;
    };

    struct range_entry
    {
        std::vector<second_level_index_entry> index;
        osmid_t from;
        osmid_t to = 0;
        std::size_t offset_from;

        range_entry(osmid_t id, std::size_t offset, std::size_t block_size)
        : from(id), offset_from(offset)
        {
            index.reserve(block_size);
        }

        bool full() const noexcept { return index.size() == index.capacity(); }
    };

    range_entry const &last() const noexcept { return m_ranges.back(); }

    bool need_new_2nd_level() const noexcept
    {
        return m_ranges.empty() || last().full();
    }

    std::pair<osmid_t, std::size_t> get_internal(osmid_t id) const noexcept;

    static constexpr std::size_t const max_block_size = 16 * 1024 * 1024;

    std::vector<range_entry> m_ranges;
    std::size_t m_block_size;
    std::size_t m_capacity = 0;
    std::size_t m_size = 0;
}; // class ordered_index_t

#endif // OSM2PGSQL_ORDERED_INDEX_HPP
