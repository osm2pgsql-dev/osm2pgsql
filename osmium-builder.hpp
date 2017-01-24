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

    explicit osmium_builder_t(std::shared_ptr<reprojection> const &proj)
    : m_proj(proj), m_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
      m_writer(m_proj->epsg(), osmium::geom::wkb_type::ewkb)
    {
    }

    wkb_t get_wkb_node(osmium::Location const &loc) const;
    wkbs_t get_wkb_split(osmium::WayNodeList const &way);
    wkb_t get_wkb_polygon(osmium::Way const &way);

    wkbs_t get_wkb_multipolygon(osmium::Relation const &rel,
                                osmium::memory::Buffer const &ways);
    wkbs_t get_wkb_multiline(osmium::memory::Buffer const &ways, bool split);

private:
    wkbs_t create_multipolygon(osmium::Area const &area);
    void add_mp_points(const osmium::NodeRefList &nodes);

    std::shared_ptr<reprojection> m_proj;
    // internal buffer for creating areas
    osmium::memory::Buffer m_buffer;
    osmium::geom::detail::WKBFactoryImpl m_writer;
};

} // namespace
