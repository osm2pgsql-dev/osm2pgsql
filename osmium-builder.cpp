#include <osmium/area/assembler.hpp>
#include <osmium/geom/wkb.hpp>

#include "osmium-builder.hpp"

namespace {

osmium::area::AssemblerConfig area_config;

inline double distance(osmium::geom::Coordinates p1,
                       osmium::geom::Coordinates p2)
{
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                             osmium::geom::Coordinates p2,
                                             double frac)
{
    return osmium::geom::Coordinates(frac * (p1.x - p2.x) + p2.x,
                                     frac * (p1.y - p2.y) + p2.y);
}
}

namespace geom {

using WKBWriter = osmium::geom::detail::WKBFactoryImpl;

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_node(osmium::Location const &loc)
{
    return m_writer.make_point(m_proj->reproject(loc));
}

osmium_builder_t::wkbs_t osmium_builder_t::get_wkb_split(osmium::Way const &way)
{
    wkbs_t ret;

    double const split_at = m_proj->target_latlon() ? 1 : 100 * 1000;

    double dist = 0;
    osmium::geom::Coordinates prev_pt;
    m_writer.linestring_start();
    size_t curlen = 0;

    for (auto const &node : way.nodes()) {
        if (!node.location().valid())
            continue;

        auto const this_pt = m_proj->reproject(node.location());
        if (prev_pt.valid()) {
            if (prev_pt == this_pt) {
                continue;
            }
            double const delta = distance(prev_pt, this_pt);

            // figure out if the addition of this point would take the total
            // length of the line in `segment` over the `split_at` distance.

            if (dist + delta > split_at) {
                size_t const splits =
                    (size_t)std::floor((dist + delta) / split_at);
                // use the splitting distance to split the current segment up
                // into as many parts as necessary to keep each part below
                // the `split_at` distance.
                osmium::geom::Coordinates ipoint;
                for (size_t j = 0; j < splits; ++j) {
                    double const frac =
                        ((double)(j + 1) * split_at - dist) / delta;
                    ipoint = interpolate(this_pt, prev_pt, frac);
                    m_writer.linestring_add_location(ipoint);
                    ret.push_back(m_writer.linestring_finish(curlen));
                    // start a new segment
                    m_writer.linestring_start();
                    m_writer.linestring_add_location(ipoint);
                }
                // reset the distance based on the final splitting point for
                // the next iteration.
                if (this_pt == ipoint) {
                    dist = 0;
                    m_writer.linestring_start();
                    curlen = 0;
                } else {
                    dist = distance(this_pt, ipoint);
                    curlen = 1;
                }
            } else {
                dist += delta;
            }
        }

        m_writer.linestring_add_location(this_pt);
        ++curlen;

        prev_pt = this_pt;
    }

    if (curlen > 1) {
        ret.push_back(m_writer.linestring_finish(curlen));
    }

    return ret;
}

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_polygon(osmium::Way const &way)
{
    osmium::area::Assembler assembler{area_config};

    m_buffer.clear();
    if (!assembler.make_area(way, m_buffer)) {
        return wkb_t();
    }

    auto wkbs = create_multipolygon(m_buffer.get<osmium::Area>(0));

    return wkbs.empty() ? wkb_t() : wkbs[0];
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multipolygon(osmium::Relation const &rel,
                                       osmium::memory::Buffer const &ways)
{
    wkbs_t ret;
    osmium::area::Assembler assembler{area_config};

    m_buffer.clear();
    if (assembler.make_area(rel, ways, m_buffer)) {
        ret = create_multipolygon(m_buffer.get<osmium::Area>(0));
    }

    return ret;
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multiline(osmium::memory::Buffer const &ways, bool)
{
    wkbs_t ret;

    // XXX need to combine ways
    // XXX need to do non-split version
    wkbs_t linewkbs;
    for (auto &w : ways.select<osmium::Way>()) {
        linewkbs = get_wkb_split(w);
        std::move(linewkbs.begin(), linewkbs.end(),
                  std::inserter(ret, ret.end()));
        linewkbs.clear();
    }

    return ret;
}

void osmium_builder_t::add_mp_points(const osmium::NodeRefList &nodes)
{
    osmium::Location last_location;
    for (const osmium::NodeRef &node_ref : nodes) {
        if (node_ref.location().valid() &&
            last_location != node_ref.location()) {
            last_location = node_ref.location();
            m_writer.multipolygon_add_location(
                m_proj->reproject(last_location));
        }
    }
}

osmium_builder_t::wkbs_t
osmium_builder_t::create_multipolygon(osmium::Area const &area)
{
    wkbs_t ret;

    // XXX need to split into polygons

    try {
        size_t num_polygons = 0;
        size_t num_rings = 0;
        m_writer.multipolygon_start();

        for (auto it = area.cbegin(); it != area.cend(); ++it) {
            if (it->type() == osmium::item_type::outer_ring) {
                auto &ring = static_cast<const osmium::OuterRing &>(*it);
                if (num_polygons > 0) {
                    m_writer.multipolygon_polygon_finish();
                }
                m_writer.multipolygon_polygon_start();
                m_writer.multipolygon_outer_ring_start();
                add_mp_points(ring);
                m_writer.multipolygon_outer_ring_finish();
                ++num_rings;
                ++num_polygons;
            } else if (it->type() == osmium::item_type::inner_ring) {
                auto &ring = static_cast<const osmium::InnerRing &>(*it);
                m_writer.multipolygon_inner_ring_start();
                add_mp_points(ring);
                m_writer.multipolygon_inner_ring_finish();
                ++num_rings;
            }
        }

        // if there are no rings, this area is invalid
        if (num_rings > 0) {
            m_writer.multipolygon_polygon_finish();
            ret.push_back(m_writer.multipolygon_finish());
        }

    } catch (osmium::geometry_error &e) { /* ignored */
    }

    return ret;
}

} // name space
