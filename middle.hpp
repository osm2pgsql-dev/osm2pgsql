/**
 * Common middle layer interface
 * Each middle layer data store must provide methods for
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include <osmium/memory/buffer.hpp>

#include <cstddef>
#include <memory>

#include <osmium/thread/pool.hpp>

#include "osmtypes.hpp"
#include "reprojection.hpp"

/**
 * Infterface for returning information about raw OSM input data from a cache.
 */
struct middle_query_t {
    virtual ~middle_query_t() = 0;

    /**
     * Retrives node locations for the given node list.
     *
     * The locations are saved directly in the input list.
     */
    virtual size_t nodes_get_list(osmium::WayNodeList *nodes) const = 0;

    /**
     * Retrives a single way from the ways storage
     * and stores it in the given osmium buffer.
     *
     * \param id     id of the way to retrive
     * \param buffer osmium buffer where to put the way
     *
     * The function does not retrieve the node locations.
     *
     * \return true if the way was retrieved
     */
    virtual bool ways_get(osmid_t id, osmium::memory::Buffer &buffer) const = 0;

    /**
     * Retrives the way members of a relation and stores them in
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
    virtual bool relations_get(osmid_t id, osmium::memory::Buffer &buffer) const = 0;

    /*
     * Retrieve a list of relations with a particular way as a member
     * \param way_id ID of the way to check
     */
    virtual idlist_t relations_using_way(osmid_t way_id) const = 0;
};

inline middle_query_t::~middle_query_t() = default;

/**
 * Interface for storing raw OSM data in an intermediate cache.
 */
struct middle_t
{
    virtual ~middle_t() = 0;

    virtual void start() = 0;
    virtual void stop(osmium::thread::Pool &pool) = 0;
    virtual void analyze(void) = 0;
    virtual void commit(void) = 0;

    virtual void nodes_set(osmium::Node const &node) = 0;
    virtual void ways_set(osmium::Way const &way) = 0;
    virtual void relations_set(osmium::Relation const &rel) = 0;

    /// Write all pending data to permanent storage.
    virtual void flush(osmium::item_type new_type) = 0;

    struct pending_processor {
        virtual ~pending_processor() {}
        virtual void enqueue_ways(osmid_t id) = 0;
        virtual void process_ways() = 0;
        virtual void enqueue_relations(osmid_t id) = 0;
        virtual void process_relations() = 0;
    };

    virtual void iterate_ways(pending_processor& pf) = 0;
    virtual void iterate_relations(pending_processor& pf) = 0;

    virtual size_t pending_count() const = 0;

    virtual std::shared_ptr<middle_query_t>
    get_query_instance(std::shared_ptr<middle_t> const &mid) const = 0;
};

inline middle_t::~middle_t() = default;

/**
 * Extended interface for permanent caching of raw OSM data.
 * It also allows updates.
 */
struct slim_middle_t : public middle_t {
    virtual ~slim_middle_t() = 0;

    virtual void nodes_delete(osmid_t id) = 0;
    virtual void node_changed(osmid_t id) = 0;

    virtual void ways_delete(osmid_t id) = 0;
    virtual void way_changed(osmid_t id) = 0;

    virtual void relations_delete(osmid_t id) = 0;
    virtual void relation_changed(osmid_t id) = 0;
};

inline slim_middle_t::~slim_middle_t() = default;

#endif
