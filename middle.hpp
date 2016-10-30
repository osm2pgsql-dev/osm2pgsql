/**
 * Common middle layer interface
 * Each middle layer data store must provide methods for
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include "osmtypes.hpp"

#include <cstddef>
#include <memory>

struct options_t;

/**
 * object which stores OSM node/ways/relations from the input file
 */
struct middle_query_t {
    virtual ~middle_query_t() {}

    virtual size_t nodes_get_list(nodelist_t &out, osmium::WayNodeList const &nds) const = 0;

    /**
     * Retrives a single way from the ways storage.
     * \return true if the way was retrieved
     * \param id id of the way to retrive
     */
    virtual bool ways_get(osmid_t id, taglist_t &tags, nodelist_t &nodes) const = 0;

    virtual size_t ways_get_list(const idlist_t &ids, idlist_t &way_ids,
                              multitaglist_t &tags,
                              multinodelist_t &nodes) const = 0;

    /**
     * Retrives a single relation from the relation storage.
     * \return true if the relation was retrieved
     * \param id id of the relation to retrive
     */
    virtual bool relations_get(osmid_t id, memberlist_t &members, taglist_t &tags) const = 0;

    /*
     * Retrieve a list of relations with a particular way as a member
     * \param way_id ID of the way to check
     */
    virtual idlist_t relations_using_way(osmid_t way_id) const = 0;

    virtual std::shared_ptr<const middle_query_t> get_instance() const = 0;
};

/**
 * A specialized middle backend which is persistent, and supports updates
 */
struct middle_t : public middle_query_t {
    static std::shared_ptr<middle_t> create_middle(bool slim);

    virtual ~middle_t() {}

    virtual void start(const options_t *out_options_) = 0;
    virtual void stop(void) = 0;
    virtual void analyze(void) = 0;
    virtual void end(void) = 0;
    virtual void commit(void) = 0;

    virtual void nodes_set(osmium::Node const &node, double lat, double lon, bool extra_tags) = 0;
    virtual void ways_set(osmium::Way const &way, bool extra_tags) = 0;
    virtual void relations_set(osmium::Relation const &rel, bool extra_tags) = 0;

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

    const options_t* out_options;
};

struct slim_middle_t : public middle_t {
    virtual ~slim_middle_t() {}

    virtual void nodes_delete(osmid_t id) = 0;
    virtual void node_changed(osmid_t id) = 0;

    virtual void ways_delete(osmid_t id) = 0;
    virtual void way_changed(osmid_t id) = 0;

    virtual void relations_delete(osmid_t id) = 0;
    virtual void relation_changed(osmid_t id) = 0;
};

#endif
