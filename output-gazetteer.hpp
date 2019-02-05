#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include <memory>
#include <string>

#include <boost/format.hpp>
#include <osmium/memory/buffer.hpp>

#include "db-copy.hpp"
#include "gazetteer-style.hpp"
#include "osmium-builder.hpp"
#include "osmtypes.hpp"
#include "output.hpp"
#include "pgsql.hpp"
#include "util.hpp"


class output_gazetteer_t : public output_t
{
    output_gazetteer_t(output_gazetteer_t const *other,
                       std::shared_ptr<middle_query_t> const &cloned_mid,
                       std::shared_ptr<db_copy_thread_t> const &copy_thread)
    : output_t(cloned_mid, other->m_options), m_copy(copy_thread),
      m_conn(nullptr), m_builder(other->m_options.projection, true),
      osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {
        connect();
        prepare_query_conn();
    }

public:
    output_gazetteer_t(std::shared_ptr<middle_query_t> const &mid,
                       options_t const &options,
                       std::shared_ptr<db_copy_thread_t> const &copy_thread)
    : output_t(mid, options), m_copy(copy_thread), m_conn(nullptr),
      m_builder(options.projection, true),
      osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {
        m_style.load_style(options.style);
    }

    virtual ~output_gazetteer_t();

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override
    {
        return std::shared_ptr<output_t>(
            new output_gazetteer_t(this, mid, copy_thread));
    }

    int start() override;
    void stop(osmium::thread::Pool *) override {}
    void commit() override;

    void enqueue_ways(pending_queue_t &, osmid_t, size_t, size_t&) override {}
    int pending_way(osmid_t, int) override { return 0; }

    void enqueue_relations(pending_queue_t &, osmid_t, size_t, size_t&) override {}
    int pending_relation(osmid_t, int) override { return 0; }

    int node_add(osmium::Node const &node) override
    {
        return process_node(node);
    }

    int way_add(osmium::Way *way) override
    {
        return process_way(way);
    }

    int relation_add(osmium::Relation const &rel) override
    {
        return process_relation(rel);
    }

    int node_modify(osmium::Node const &node) override
    {
        return process_node(node);
    }

    int way_modify(osmium::Way *way) override
    {
        return process_way(way);
    }

    int relation_modify(osmium::Relation const &rel) override
    {
        return process_relation(rel);
    }

    int node_delete(osmid_t id) override
    {
        delete_place("N", id);
        return 0;
    }

    int way_delete(osmid_t id) override
    {
        delete_place("W", id);
        return 0;
    }

    int relation_delete(osmid_t id) override
    {
        delete_place("R", id);
        return 0;
    }

private:
    enum { PLACE_BUFFER_SIZE = 4096 };

    /// Delete all places that are not covered by the current style results.
    void delete_unused_classes(char const *osm_type, osmid_t osm_id);
    /// Delete all places for the given OSM object.
    void delete_place(char const *osm_type, osmid_t osm_id);
    int process_node(osmium::Node const &node);
    int process_way(osmium::Way *way);
    int process_relation(osmium::Relation const &rel);
    void connect();

    void prepare_query_conn() const;

    void delete_unused_full(char const *osm_type, osmid_t osm_id)
    {
        if (m_options.append) {
            delete_place(osm_type, osm_id);
        }
    }

    db_copy_mgr_t m_copy;
    struct pg_conn *m_conn;
    gazetteer_style_t m_style;

    geom::osmium_builder_t m_builder;
    osmium::memory::Buffer osmium_buffer;
};

#endif
