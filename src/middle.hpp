#ifndef OSM2PGSQL_MIDDLE_HPP
#define OSM2PGSQL_MIDDLE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <osmium/memory/buffer.hpp>
#include <osmium/osm/entity_bits.hpp>

#include <cstdint>
#include <memory>

#include "osmtypes.hpp"
#include "thread-pool.hpp"

class idlist_t;

struct options_t;
struct output_requirements;

/**
 * Interface for returning information about raw OSM input data from a cache.
 */
struct middle_query_t : std::enable_shared_from_this<middle_query_t>
{
    middle_query_t() noexcept = default;

    virtual ~middle_query_t() = 0;

    middle_query_t(middle_query_t const &) = delete;
    middle_query_t &operator=(middle_query_t const &) = delete;
    middle_query_t(middle_query_t &&) = delete;
    middle_query_t &operator=(middle_query_t &&) = delete;

    /**
     * Retrieves node location for the given id.
     */
    virtual osmium::Location get_node_location(osmid_t id) const = 0;

    /**
     * Retrieves node locations for the given node list.
     *
     * The locations are saved directly in the input list.
     */
    virtual size_t nodes_get_list(osmium::WayNodeList *nodes) const = 0;

    /**
     * Retrieves a single node from the nodes storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the node to retrieve
     * \param buffer osmium buffer where to put the node
     *
     * \return true if the node was retrieved
     */
    virtual bool node_get(osmid_t id, osmium::memory::Buffer *buffer) const = 0;

    /**
     * Retrieves a single way from the ways storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the way to retrieve
     * \param buffer osmium buffer where to put the way
     *
     * The function does not retrieve the node locations.
     *
     * \return true if the way was retrieved
     */
    virtual bool way_get(osmid_t id, osmium::memory::Buffer *buffer) const = 0;

    /**
     * Retrieves the members of a relation and stores them in an Osmium
     * buffer. If a member is not available that is not an error.
     *
     * \param      rel    Relation to get the members for.
     * \param[out] buffer Buffer where to store the members in.
     * \param      types  The types of members we are interested in.
     *
     * \return The number of members we could get.
     */
    virtual size_t
    rel_members_get(osmium::Relation const &rel, osmium::memory::Buffer *buffer,
                    osmium::osm_entity_bits::type types) const = 0;

    /**
     * Retrieves a single relation from the relation storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the relation to retrieve
     * \param buffer osmium buffer where to put the relation
     *
     * \return true if the relation was retrieved
     */
    virtual bool relation_get(osmid_t id,
                              osmium::memory::Buffer *buffer) const = 0;
};

/**
 * Interface for storing "raw" OSM data in an intermediate object store and
 * getting it back.
 */
class middle_t
{
public:
    explicit middle_t(std::shared_ptr<thread_pool_t> thread_pool)
    : m_thread_pool(std::move(thread_pool))
    {}

    virtual ~middle_t() = 0;

    middle_t(middle_t const &) = delete;
    middle_t &operator=(middle_t const &) = delete;
    middle_t(middle_t &&) = delete;
    middle_t &operator=(middle_t &&) = delete;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void wait() {}

    /// This is called for every added, changed or deleted node.
    virtual void node(osmium::Node const &node) = 0;

    /// This is called for every added, changed or deleted way.
    virtual void way(osmium::Way const &way) = 0;

    /// This is called for every added, changed or deleted relation.
    virtual void relation(osmium::Relation const &relation) = 0;

    /// Called after all nodes from the input file(s) have been processed.
    virtual void after_nodes()
    {
        assert(m_middle_state == middle_state::node);
#ifndef NDEBUG
        m_middle_state = middle_state::way;
#endif
    }

    /// Called after all ways from the input file(s) have been processed.
    virtual void after_ways()
    {
        assert(m_middle_state == middle_state::way);
#ifndef NDEBUG
        m_middle_state = middle_state::relation;
#endif
    }

    /// Called after all relations from the input file(s) have been processed.
    virtual void after_relations()
    {
        assert(m_middle_state == middle_state::relation);
#ifndef NDEBUG
        m_middle_state = middle_state::done;
#endif
    }

    virtual void get_node_parents(idlist_t const & /*changed_nodes*/,
                                  idlist_t * /*parent_ways*/,
                                  idlist_t * /*parent_relations*/) const
    {
    }

    virtual void get_way_parents(idlist_t const & /*changed_ways*/,
                                 idlist_t * /*parent_relations*/) const
    {
    }

    virtual std::shared_ptr<middle_query_t> get_query_instance() = 0;

    virtual void set_requirements(output_requirements const &) {}

protected:
    thread_pool_t &thread_pool() const noexcept
    {
        assert(m_thread_pool);
        return *m_thread_pool;
    }

#ifndef NDEBUG
    enum class middle_state : uint8_t
    {
        constructed,
        started,
        node,
        way,
        relation,
        done
    };

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes, misc-non-private-member-variables-in-classes)
    middle_state m_middle_state = middle_state::constructed;
#endif

private:
    std::shared_ptr<thread_pool_t> m_thread_pool;
}; // class middle_t

/// Factory function: Instantiate the middle based on the command line options.
std::shared_ptr<middle_t>
create_middle(std::shared_ptr<thread_pool_t> thread_pool,
              options_t const &options);

#endif // OSM2PGSQL_MIDDLE_HPP
