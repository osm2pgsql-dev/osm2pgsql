#ifndef OSM2PGSQL_OSMIUM_BUILDER_HPP
#define OSM2PGSQL_OSMIUM_BUILDER_HPP

#include <memory>
#include <string>
#include <vector>

#include <osmium/memory/buffer.hpp>
#include <osmium/osm.hpp>

#include "reprojection.hpp"
#include "wkb.hpp"

namespace geom {

class osmium_builder_t
{
public:
    using wkb_t = std::string;
    using wkbs_t = std::vector<std::string>;

    explicit osmium_builder_t(std::shared_ptr<reprojection> const &proj)
    : m_proj(proj), m_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
      m_writer(m_proj->target_srs())
    {}

    wkb_t get_wkb_node(osmium::Location const &loc) const;
    wkbs_t get_wkb_line(osmium::WayNodeList const &nodes, double split_at);
    wkb_t get_wkb_polygon(osmium::Way const &way);

    wkbs_t get_wkb_multipolygon(osmium::Relation const &rel,
                                osmium::memory::Buffer const &ways,
                                bool build_multigeoms);

    wkbs_t get_wkb_multiline(osmium::memory::Buffer const &ways,
                             double split_at);

private:
    wkb_t create_multipolygon(osmium::Area const &area);
    wkbs_t create_polygons(osmium::Area const &area);
    size_t add_mp_points(osmium::NodeRefList const &nodes);

    std::shared_ptr<reprojection> m_proj;
    // internal buffer for creating areas
    osmium::memory::Buffer m_buffer;
    ewkb::writer_t m_writer;
};

} // namespace geom

#endif // OSM2PGSQL_OSMIUM_BUILDER_HPP
