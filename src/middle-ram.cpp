/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"
#include "middle-ram.hpp"
#include "options.hpp"

#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/util/delta.hpp>

// Workaround: This must be included before buffer_string.hpp due to a missing
// include in the upstream code. https://github.com/mapbox/protozero/pull/104
#include <protozero/config.hpp>

#include <protozero/buffer_string.hpp>
#include <protozero/varint.hpp>

#include <cassert>
#include <memory>

middle_ram_t::middle_ram_t(std::shared_ptr<thread_pool_t> thread_pool,
                           options_t const *options)
: middle_t(std::move(thread_pool))
{
    assert(options);

    if (options->extra_attributes) {
        m_store_options.untagged_nodes = true;
    }
}

void middle_ram_t::set_requirements(output_requirements const &requirements)
{
    if (requirements.full_nodes) {
        m_store_options.nodes = true;
    }

    if (requirements.full_ways) {
        m_store_options.ways = true;
        m_store_options.way_nodes = false;
    }

    if (requirements.full_relations) {
        m_store_options.relations = true;
    }

    log_debug("Middle 'ram' options:");
    log_debug("  locations: {}", m_store_options.locations);
    log_debug("  way_nodes: {}", m_store_options.way_nodes);
    log_debug("  nodes: {}", m_store_options.nodes);
    log_debug("  untagged_nodes: {}", m_store_options.untagged_nodes);
    log_debug("  ways: {}", m_store_options.ways);
    log_debug("  relations: {}", m_store_options.relations);
}

void middle_ram_t::stop()
{
    auto const mbyte = 1024 * 1024;

    log_debug("Middle 'ram': Node locations: size={} bytes={}M",
              m_node_locations.size(), m_node_locations.used_memory() / mbyte);

    log_debug("Middle 'ram': Way nodes data: size={} capacity={} bytes={}M",
              m_way_nodes_data.size(), m_way_nodes_data.capacity(),
              m_way_nodes_data.capacity() / mbyte);

    log_debug("Middle 'ram': Way nodes index: size={} capacity={} bytes={}M",
              m_way_nodes_index.size(), m_way_nodes_index.capacity(),
              m_way_nodes_index.used_memory() / mbyte);

    log_debug("Middle 'ram': Object data: size={} capacity={} bytes={}M",
              m_object_buffer.committed(), m_object_buffer.capacity(),
              m_object_buffer.capacity() / mbyte);

    std::size_t index_size = 0;
    std::size_t index_capacity = 0;
    std::size_t index_mem = 0;
    for (auto const &index : m_object_index) {
        index_size += index.size();
        index_capacity += index.capacity();
        index_mem += index.used_memory();
    }
    log_debug("Middle 'ram': Object indexes: size={} capacity={} bytes={}M",
              index_size, index_capacity, index_mem / mbyte);

    log_debug("Middle 'ram': Memory used overall: {}MBytes",
              (m_node_locations.used_memory() + m_way_nodes_data.capacity() +
               m_way_nodes_index.used_memory() + m_object_buffer.capacity() +
               index_mem) /
                  mbyte);

    m_node_locations.clear();

    m_way_nodes_index.clear();
    m_way_nodes_data.clear();
    m_way_nodes_data.shrink_to_fit();

    m_object_buffer = osmium::memory::Buffer{};

    for (auto &index : m_object_index) {
        index.clear();
    }
}

void middle_ram_t::store_object(osmium::OSMObject const &object)
{
    auto const offset = m_object_buffer.committed();
    m_object_buffer.add_item(object);
    m_object_buffer.commit();
    m_object_index(object.type()).add(object.id(), offset);
}

bool middle_ram_t::get_object(osmium::item_type type, osmid_t id,
                              osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    auto const offset = m_object_index(type).get(id);
    if (offset == ordered_index_t::not_found_value()) {
        return false;
    }
    buffer->add_item(m_object_buffer.get<osmium::memory::Item>(offset));
    buffer->commit();
    return true;
}

static void add_delta_encoded_way_node_list(std::string *data,
                                            osmium::WayNodeList const &wnl)
{
    assert(data);

    // Add number of nodes in list
    protozero::add_varint_to_buffer(data, wnl.size());

    // Add delta encoded node ids
    osmium::DeltaEncode<osmid_t> delta;
    for (auto const &nr : wnl) {
        protozero::add_varint_to_buffer(
            data, protozero::encode_zigzag64(delta.update(nr.ref())));
    }
}

void middle_ram_t::node(osmium::Node const &node)
{
    assert(node.visible());

    if (m_store_options.locations) {
        m_node_locations.set(node.id(), node.location());
    }

    if (m_store_options.nodes &&
        (!node.tags().empty() || m_store_options.untagged_nodes)) {
        store_object(node);
    }
}

void middle_ram_t::way(osmium::Way const &way)
{
    assert(way.visible());

    if (m_store_options.way_nodes) {
        auto const offset = m_way_nodes_data.size();
        add_delta_encoded_way_node_list(&m_way_nodes_data, way.nodes());
        m_way_nodes_index.add(way.id(), offset);
    }

    if (m_store_options.ways) {
        store_object(way);
    }
}

void middle_ram_t::relation(osmium::Relation const &relation)
{
    assert(relation.visible());

    if (m_store_options.relations) {
        store_object(relation);
    }
}

std::size_t middle_ram_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    assert(nodes);

    std::size_t count = 0;

    if (m_store_options.locations) {
        for (auto &nr : *nodes) {
            nr.set_location(m_node_locations.get(nr.ref()));
            if (nr.location().valid()) {
                ++count;
            }
        }
    }

    return count;
}

bool middle_ram_t::way_get(osmid_t id, osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    if (m_store_options.ways) {
        return get_object(osmium::item_type::way, id, buffer);
    }
    return false;
}

static void
get_delta_encoded_way_nodes_list(std::string const &data, std::size_t offset,
                                 osmium::builder::WayBuilder *builder)
{
    assert(builder);

    char const *begin = data.data() + offset;
    char const *const end = data.data() + data.size();

    auto count = protozero::decode_varint(&begin, end);

    osmium::DeltaDecode<osmid_t> delta;
    osmium::builder::WayNodeListBuilder wnl_builder{*builder};
    while (count > 0) {
        auto const val =
            protozero::decode_zigzag64(protozero::decode_varint(&begin, end));
        wnl_builder.add_node_ref(delta.update(val));
        --count;
    }
}

std::size_t
middle_ram_t::rel_members_get(osmium::Relation const &rel,
                              osmium::memory::Buffer *buffer,
                              osmium::osm_entity_bits::type types) const
{
    assert(buffer);

    std::size_t count = 0;

    for (auto const &member : rel.members()) {
        auto const member_entity_type =
            osmium::osm_entity_bits::from_item_type(member.type());
        if ((member_entity_type & types) == 0) {
            continue;
        }

        switch (member.type()) {
        case osmium::item_type::node:
            if (m_store_options.nodes) {
                auto const offset =
                    m_object_index.nodes().get(member.ref());
                if (offset != ordered_index_t::not_found_value()) {
                    buffer->add_item(m_object_buffer.get<osmium::Node>(offset));
                    buffer->commit();
                    ++count;
                }
            }
            break;
        case osmium::item_type::way:
            if (m_store_options.ways) {
                auto const offset =
                    m_object_index.ways().get(member.ref());
                if (offset != ordered_index_t::not_found_value()) {
                    buffer->add_item(m_object_buffer.get<osmium::Way>(offset));
                    buffer->commit();
                    ++count;
                }
            } else if (m_store_options.way_nodes) {
                auto const offset = m_way_nodes_index.get(member.ref());
                if (offset != ordered_index_t::not_found_value()) {
                    osmium::builder::WayBuilder builder{*buffer};
                    builder.set_id(member.ref());
                    get_delta_encoded_way_nodes_list(m_way_nodes_data, offset,
                                                     &builder);
                }
                buffer->commit();
                ++count;
            }
            break;
        default: // osmium::item_type::relation
            if (m_store_options.relations) {
                auto const offset =
                    m_object_index.relations().get(member.ref());
                if (offset != ordered_index_t::not_found_value()) {
                    buffer->add_item(
                        m_object_buffer.get<osmium::Relation>(offset));
                    buffer->commit();
                    ++count;
                }
            }
        }
    }

    return count;
}

bool middle_ram_t::relation_get(osmid_t id,
                                osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    if (m_store_options.relations) {
        return get_object(osmium::item_type::relation, id, buffer);
    }
    return false;
}

std::shared_ptr<middle_query_t> middle_ram_t::get_query_instance()
{
    return shared_from_this();
}
