#ifndef OSM2PGSQL_MIDDLE_HPP
#define OSM2PGSQL_MIDDLE_HPP

#include <osmium/memory/buffer.hpp>

#include <memory>

#include "osmtypes.hpp"
#include "thread-pool.hpp"

/**
 * Interface for returning information about raw OSM input data from a cache.
 */
struct middle_query_t : std::enable_shared_from_this<middle_query_t>
{
    virtual ~middle_query_t() = 0;

    /**
     * Retrieves node locations for the given node list.
     *
     * The locations are saved directly in the input list.
     */
    virtual size_t nodes_get_list(osmium::WayNodeList *nodes) const = 0;

    /**
     * Retrieves a single way from the ways storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the way to retrive
     * \param buffer osmium buffer where to put the way
     *
     * The function does not retrieve the node locations.
     *
     * \return true if the way was retrieved
     */
    virtual bool way_get(osmid_t id, osmium::memory::Buffer &buffer) const = 0;

    /**
     * Retrieves the way members of a relation and stores them in
     * the given osmium buffer.
     *
     * \param      rel    Relation to get the members for.
     * \param[out] roles  Roles for the ways that where retrived.
     * \param[out] buffer Buffer where to store the members in.
     */
    virtual size_t
    rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                        osmium::memory::Buffer &buffer) const = 0;

    /**
     * Retrives a single relation from the relation storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the relation to retrive
     * \param buffer osmium buffer where to put the relation
     *
     * \return true if the relation was retrieved
     */
    virtual bool relation_get(osmid_t id,
                              osmium::memory::Buffer &buffer) const = 0;
};

inline middle_query_t::~middle_query_t() = default;

/**
 * Interface for storing "raw" OSM data in an intermediate object store and
 * getting it back.
 */
struct middle_t
{
    virtual ~middle_t() = 0;

    virtual void start() = 0;
    virtual void stop(thread_pool_t &pool) = 0;
    virtual void analyze() = 0;
    virtual void commit() = 0;

    /// This is called for every added, changed or deleted node.
    virtual void node(osmium::Node const &node) = 0;

    /// This is called for every added, changed or deleted way.
    virtual void way(osmium::Way const &way) = 0;

    /// This is called for every added, changed or deleted relation.
    virtual void relation(osmium::Relation const &relation) = 0;

    /**
     * Ensure all pending data is written to the storage.
     *
     * You can only query objects from the storage after they have been
     * flushed.
     *
     * The function is called after setting all the nodes, then after setting
     * all the ways, and again after setting all the relations.
     */
    virtual void flush() = 0;

    virtual idlist_t get_ways_by_node(osmid_t) { return {}; }
    virtual idlist_t get_rels_by_node(osmid_t) { return {}; }
    virtual idlist_t get_rels_by_way(osmid_t) { return {}; }

    virtual std::shared_ptr<middle_query_t> get_query_instance() = 0;
};

inline middle_t::~middle_t() = default;

#endif // OSM2PGSQL_MIDDLE_HPP
