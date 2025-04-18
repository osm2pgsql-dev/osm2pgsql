#ifndef OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP
#define OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <memory>
#include <string>

#include <osmium/index/map/dense_file_array.hpp>
#include <osmium/osm/location.hpp>

#include "osmtypes.hpp"

class node_persistent_cache
{
public:
    node_persistent_cache(std::string file_name, bool create_file,
                          bool remove_file);
    ~node_persistent_cache() noexcept;

    node_persistent_cache(node_persistent_cache const &) = delete;
    node_persistent_cache &operator=(node_persistent_cache const &) = delete;

    node_persistent_cache(node_persistent_cache &&) = delete;
    node_persistent_cache &operator=(node_persistent_cache &&) = delete;

    void set(osmid_t id, osmium::Location location);
    osmium::Location get(osmid_t id) const noexcept;

    /// The number of locations stored.
    std::size_t size() const { return m_index->size(); }

    /// Return the approximate number of bytes used for internal storage.
    std::size_t used_memory() const { return m_index->used_memory(); }

private:
    using index_t =
        osmium::index::map::DenseFileArray<osmium::unsigned_object_id_type,
                                           osmium::Location>;

    std::string m_file_name;
    int m_fd = -1;
    std::unique_ptr<index_t> m_index;
    bool m_remove_file;
};

#endif // OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP
