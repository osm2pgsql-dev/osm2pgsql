#ifndef OSM2PGSQL_MIDDLE_RAM_HPP
#define OSM2PGSQL_MIDDLE_RAM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "middle.hpp"
#include "node-locations.hpp"
#include "osmtypes.hpp"
#include "ordered-index.hpp"

#include <osmium/index/nwr_array.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm.hpp>

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

class node_persistent_cache;
class thread_pool_t;

/**
 * Implementation of middle for importing small to medium sized files into a
 * non-updateable database. It works completely in memory, no data is written
 * to disk.
 *
 * The following traits of OSM objects can be stored. All are optional:
 * - Node locations for building geometries of ways.
 * - Way node ids for building geometries of relations based on ways.
 * - Tags and attributes for nodes, ways, and/or relations for full
 *   2-stage-processing support.
 * - Attributes for untagged nodes.
 */
class middle_ram_t : public middle_t, public middle_query_t
{
public:
    middle_ram_t(std::shared_ptr<thread_pool_t> thread_pool,
                 options_t const *options);

    ~middle_ram_t() noexcept override = default;

    void start() override
    {
        assert(m_middle_state == middle_state::constructed);
#ifndef NDEBUG
        m_middle_state = middle_state::node;
#endif
    }

    void stop() override;

    void node(osmium::Node const &node) override;
    void way(osmium::Way const &way) override;
    void relation(osmium::Relation const &) override;

    void after_nodes() override;

    osmium::Location get_node_location(osmid_t id) const override;

    std::size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    bool node_get(osmid_t id, osmium::memory::Buffer *buffer) const override;

    bool way_get(osmid_t id, osmium::memory::Buffer *buffer) const override;

    size_t rel_members_get(osmium::Relation const &rel,
                           osmium::memory::Buffer *buffer,
                           osmium::osm_entity_bits::type types) const override;

    bool relation_get(osmid_t id,
                      osmium::memory::Buffer *buffer) const override;

    std::shared_ptr<middle_query_t> get_query_instance() override;

    void set_requirements(output_requirements const &requirements) override;

private:
    struct middle_ram_options
    {
        // Store node locations in special node location store.
        bool locations = true;

        // Store way nodes in special way nodes store.
        bool way_nodes = true;

        // Store nodes (with tags, attributes, and location) in object store.
        bool nodes = false;

        // Store untagged nodes also (set in addition to nodes=true).
        bool untagged_nodes = false;

        // Store ways (with tags, attributes, and way nodes) in object store.
        bool ways = false;

        // Store relations (with tags, attributes, and members) in object store.
        bool relations = false;
    };

    void store_object(osmium::OSMObject const &object);

    bool get_object(osmium::item_type type, osmid_t id,
                    osmium::memory::Buffer *buffer) const;

    /// For storing the location of all nodes.
    node_locations_t m_node_locations;

    /// For storing the node lists of all ways.
    std::string m_way_nodes_data;

    /// The index for accessing way nodes.
    ordered_index_t m_way_nodes_index;

    /// Buffer for all OSM objects we store.
    osmium::memory::Buffer m_object_buffer{
        1024UL * 1024UL, osmium::memory::Buffer::auto_grow::yes};

    /// Indexes into object buffer.
    osmium::nwr_array<ordered_index_t> m_object_index;

    /// Options for this middle.
    middle_ram_options m_store_options;

    /// File cache
    std::shared_ptr<node_persistent_cache> m_persistent_cache;

}; // class middle_ram_t

#endif // OSM2PGSQL_MIDDLE_RAM_HPP
