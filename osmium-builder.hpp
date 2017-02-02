#ifndef OSMIUM_BUILDER_H
#define OSMIUM_BUILDER_H

#include <memory>
#include <string>
#include <vector>

#include <osmium/geom/wkb.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm.hpp>

#include "reprojection.hpp"

namespace geom {

class osmium_builder_t
{
public:
    typedef std::string wkb_t;
    typedef std::vector<std::string> wkbs_t;

    explicit osmium_builder_t(std::shared_ptr<reprojection> const &proj,
                              bool build_multigeoms)
    : m_proj(proj), m_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
      m_writer(m_proj->epsg(), osmium::geom::wkb_type::ewkb),
      m_build_multigeoms(build_multigeoms)
    {
    }

    wkb_t get_wkb_node(osmium::Location const &loc) const;
    wkbs_t get_wkb_line(osmium::WayNodeList const &way, bool do_split);
    wkb_t get_wkb_polygon(osmium::Way const &way);

    wkbs_t get_wkb_multipolygon(osmium::Relation const &rel,
                                osmium::memory::Buffer const &ways);
    wkbs_t get_wkb_multiline(osmium::memory::Buffer const &ways, bool split);

private:
    wkb_t create_multipolygon(osmium::Area const &area);
    wkbs_t create_polygons(osmium::Area const &area);
    void add_mp_points(const osmium::NodeRefList &nodes);

    std::shared_ptr<reprojection> m_proj;
    // internal buffer for creating areas
    osmium::memory::Buffer m_buffer;
    osmium::geom::detail::WKBFactoryImpl m_writer;
    bool m_build_multigeoms;
};

} // namespace

#endif // OSMIUM_BUILDER_H
