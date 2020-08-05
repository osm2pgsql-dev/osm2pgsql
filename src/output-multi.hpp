#ifndef OSM2PGSQL_OUTPUT_MULTI_HPP
#define OSM2PGSQL_OUTPUT_MULTI_HPP

/* One implementation of output-layer processing for osm2pgsql.
 * Manages a single table, transforming geometry using a
 * variety of algorithms plus tag transformation for the
 * database columns.
 */

#include "expire-tiles.hpp"
#include "geometry-processor.hpp"
#include "id-tracker.hpp"
#include "osmtypes.hpp"
#include "output.hpp"

#include <cstddef>
#include <memory>
#include <string>

class options_t;
class table_t;
class tagtransform_t;
class export_list;
struct middle_query_t;

class output_multi_t : public output_t
{
    output_multi_t(output_multi_t const *other,
                   std::shared_ptr<middle_query_t> const &mid,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);

public:
    output_multi_t(std::string const &name,
                   std::shared_ptr<geometry_processor> processor_,
                   export_list const &export_list,
                   std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);
    virtual ~output_multi_t();

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override;
    void stop(thread_pool_t *pool) override;
    void sync() override;

    void pending_way(osmid_t id) override;
    void pending_relation(osmid_t id) override;

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const &node) override;
    void way_modify(osmium::Way *way) override;
    void relation_modify(osmium::Relation const &rel) override;

    void node_delete(osmid_t id) override;
    void way_delete(osmid_t id) override;
    void relation_delete(osmid_t id) override;

    void merge_expire_trees(output_t *other) override;

protected:
    void delete_from_output(osmid_t id);
    void process_node(osmium::Node const &node);
    void process_way(osmium::Way *way);
    void reprocess_way(osmium::Way *way, bool exists);
    void process_relation(osmium::Relation const &rel, bool exists);
    void copy_node_to_table(osmid_t id, const std::string &geom,
                            taglist_t &tags);
    void copy_to_table(osmid_t const id, geometry_processor::wkb_t const &geom,
                       taglist_t &tags);

    std::unique_ptr<tagtransform_t> m_tagtransform;
    std::shared_ptr<geometry_processor> m_processor;
    std::shared_ptr<reprojection> m_proj;
    osmium::item_type const m_osm_type;
    std::unique_ptr<table_t> m_table;
    expire_tiles m_expire;
    relation_helper m_relation_helper;
    osmium::memory::Buffer buffer;
    geom::osmium_builder_t m_builder;
    bool m_way_area;
};

#endif // OSM2PGSQL_OUTPUT_MULTI_HPP
