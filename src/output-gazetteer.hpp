#ifndef OSM2PGSQL_OUTPUT_GAZETTEER_HPP
#define OSM2PGSQL_OUTPUT_GAZETTEER_HPP

#include <memory>

#include <osmium/memory/buffer.hpp>

#include "db-copy-mgr.hpp"
#include "gazetteer-style.hpp"
#include "osmium-builder.hpp"
#include "osmtypes.hpp"
#include "output.hpp"

class output_gazetteer_t : public output_t
{
    output_gazetteer_t(output_gazetteer_t const *other,
                       std::shared_ptr<middle_query_t> const &cloned_mid,
                       std::shared_ptr<db_copy_thread_t> const &copy_thread)
    : output_t(cloned_mid, other->m_options), m_copy(copy_thread),
      m_builder(other->m_options.projection),
      m_osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {}

public:
    output_gazetteer_t(std::shared_ptr<middle_query_t> const &mid,
                       options_t const &options,
                       std::shared_ptr<db_copy_thread_t> const &copy_thread)
    : output_t(mid, options), m_copy(copy_thread),
      m_builder(options.projection),
      m_osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {
        m_style.load_style(options.style);
    }

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override
    {
        return std::shared_ptr<output_t>(
            new output_gazetteer_t{this, mid, copy_thread});
    }

    void start() override;
    void stop(thread_pool_t *) noexcept override {}
    void sync() override;

    void pending_way(osmid_t) noexcept override {}
    void pending_relation(osmid_t) noexcept override {}

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const &node) override;
    void way_modify(osmium::Way *way) override;
    void relation_modify(osmium::Relation const &rel) override;

    void node_delete(osmid_t id) override { delete_unused_full('N', id); }
    void way_delete(osmid_t id) override { delete_unused_full('W', id); }
    void relation_delete(osmid_t id) override { delete_unused_full('R', id); }

private:
    enum
    {
        PLACE_BUFFER_SIZE = 4096
    };

    /// Delete all places that are not covered by the current style results.
    void delete_unused_classes(char osm_type, osmid_t osm_id);
    /// Delete all places for the given OSM object.
    void delete_unused_full(char osm_type, osmid_t osm_id);
    bool process_node(osmium::Node const &node);
    bool process_way(osmium::Way *way);
    bool process_relation(osmium::Relation const &rel);

    gazetteer_copy_mgr_t m_copy;
    gazetteer_style_t m_style;

    geom::osmium_builder_t m_builder;
    osmium::memory::Buffer m_osmium_buffer;
};

#endif // OSM2PGSQL_OUTPUT_GAZETTEER_HPP
