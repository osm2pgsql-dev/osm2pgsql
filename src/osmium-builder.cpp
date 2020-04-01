#include <algorithm>
#include <cassert>
#include <tuple>
#include <vector>

#include <osmium/area/geom_assembler.hpp>

#include "osmium-builder.hpp"

namespace {

inline double distance(osmium::geom::Coordinates p1,
                       osmium::geom::Coordinates p2)
{
    double const dx = p1.x - p2.x;
    double const dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                             osmium::geom::Coordinates p2,
                                             double frac) noexcept
{
    return osmium::geom::Coordinates{frac * (p1.x - p2.x) + p2.x,
                                     frac * (p1.y - p2.y) + p2.y};
}

template <typename ITERATOR>
void add_nodes_to_builder(osmium::builder::WayNodeListBuilder &builder,
                          ITERATOR const &begin, ITERATOR const &end,
                          bool skip_first)
{
    auto it = begin;
    if (skip_first) {
        ++it;
    }

    while (it != end) {
        if (it->location().valid()) {
            builder.add_node_ref(*it);
        }
        ++it;
    }
}

} // namespace

namespace geom {

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_node(osmium::Location const &loc) const
{
    return m_writer.make_point(m_proj->reproject(loc));
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_line(osmium::WayNodeList const &nodes,
                               double split_at)
{
    wkbs_t ret;

    bool const do_split = split_at > 0.0;

    double dist = 0;
    osmium::geom::Coordinates prev_pt;
    m_writer.linestring_start();
    size_t curlen = 0;

    for (auto const &node : nodes) {
        if (!node.location().valid()) {
            continue;
        }

        auto const this_pt = m_proj->reproject(node.location());
        if (prev_pt.valid()) {
            if (prev_pt == this_pt) {
                continue;
            }

            if (do_split) {
                double const delta = distance(prev_pt, this_pt);

                // figure out if the addition of this point would take the total
                // length of the line in `segment` over the `split_at` distance.

                if (dist + delta > split_at) {
                    auto const splits =
                        (size_t)std::floor((dist + delta) / split_at);
                    // use the splitting distance to split the current segment up
                    // into as many parts as necessary to keep each part below
                    // the `split_at` distance.
                    osmium::geom::Coordinates ipoint;
                    for (size_t j = 0; j < splits; ++j) {
                        double const frac =
                            ((double)(j + 1) * split_at - dist) / delta;
                        ipoint = interpolate(this_pt, prev_pt, frac);
                        m_writer.add_location(ipoint);
                        ret.push_back(m_writer.linestring_finish(curlen + 1));
                        // start a new segment
                        m_writer.linestring_start();
                        m_writer.add_location(ipoint);
                        curlen = 1;
                    }
                    // reset the distance based on the final splitting point for
                    // the next iteration.
                    if (this_pt == ipoint) {
                        dist = 0;
                        m_writer.linestring_finish(0);
                        m_writer.linestring_start();
                        curlen = 0;
                    } else {
                        dist = distance(this_pt, ipoint);
                    }
                } else {
                    dist += delta;
                }
            }
        }

        m_writer.add_location(this_pt);
        ++curlen;

        prev_pt = this_pt;
    }

    auto const wkb = m_writer.linestring_finish(curlen);
    if (curlen > 1) {
        ret.push_back(wkb);
    }

    return ret;
}

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_polygon(osmium::Way const &way)
{
    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};

    m_buffer.clear();
    if (!assembler(way, m_buffer)) {
        return wkb_t();
    }

    auto const wkbs = create_polygons(m_buffer.get<osmium::Area>(0));

    return wkbs.empty() ? wkb_t() : wkbs[0];
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multipolygon(osmium::Relation const &rel,
                                       osmium::memory::Buffer const &ways,
                                       bool build_multigeoms)
{
    wkbs_t ret;
    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};

    m_buffer.clear();
    if (assembler(rel, ways, m_buffer)) {
        if (build_multigeoms) {
            ret.push_back(create_multipolygon(m_buffer.get<osmium::Area>(0)));
        } else {
            ret = create_polygons(m_buffer.get<osmium::Area>(0));
        }
    }

    return ret;
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multiline(osmium::memory::Buffer const &ways,
                                    double split_at)
{
    // make a list of all endpoints
    using endpoint_t = std::tuple<osmium::object_id_type, size_t, bool>;
    std::vector<endpoint_t> endpoints;
    // and a list of way connections
    enum lmt : size_t
    {
        NOCONN = -1UL
    };
    std::vector<std::tuple<size_t, osmium::Way const *, size_t>> conns;

    // initialise the two lists
    for (auto const &w : ways.select<osmium::Way>()) {
        if (w.nodes().size() > 1) {
            endpoints.emplace_back(w.nodes().front().ref(), conns.size(), true);
            endpoints.emplace_back(w.nodes().back().ref(), conns.size(), false);
            conns.emplace_back(NOCONN, &w, NOCONN);
        }
    }
    // sort by node id
    std::sort(endpoints.begin(), endpoints.end());
    // now fill the connection list based on the sorted list
    {
        endpoint_t const *prev = nullptr;
        for (auto const &pt : endpoints) {
            if (prev) {
                if (std::get<0>(*prev) == std::get<0>(pt)) {
                    auto const previd = std::get<1>(*prev);
                    auto const ptid = std::get<1>(pt);
                    if (std::get<2>(*prev)) {
                        std::get<0>(conns[previd]) = ptid;
                    } else {
                        std::get<2>(conns[previd]) = ptid;
                    }
                    if (std::get<2>(pt)) {
                        std::get<0>(conns[ptid]) = previd;
                    } else {
                        std::get<2>(conns[ptid]) = previd;
                    }
                    prev = nullptr;
                    continue;
                }
            }

            prev = &pt;
        }
    }

    wkbs_t ret;

    size_t done_ways = 0;
    size_t const todo_ways = conns.size();
    for (size_t i = 0; i < todo_ways; ++i) {
        if (!std::get<1>(conns[i]) || (std::get<0>(conns[i]) != NOCONN &&
                                       std::get<2>(conns[i]) != NOCONN)) {
            continue; // way already done or not the beginning of a segment
        }

        m_buffer.clear();
        {
            osmium::builder::WayNodeListBuilder wnl_builder{m_buffer};
            size_t prev = NOCONN;
            size_t cur = i;

            do {
                auto &conn = conns[cur];
                assert(std::get<1>(conn));
                auto &nl = std::get<1>(conn)->nodes();
                bool const skip_first = prev != NOCONN;
                bool const forward = std::get<0>(conn) == prev;
                prev = cur;
                // add way nodes
                if (forward) {
                    add_nodes_to_builder(wnl_builder, nl.cbegin(), nl.cend(),
                                         skip_first);
                    cur = std::get<2>(conn);
                } else {
                    add_nodes_to_builder(wnl_builder, nl.crbegin(), nl.crend(),
                                         skip_first);
                    cur = std::get<0>(conn);
                }
                // mark way as done
                std::get<1>(conns[prev]) = nullptr;
                ++done_ways;
            } while (cur != NOCONN);
        }

        // found a line end, create the wkbs
        m_buffer.commit();
        auto linewkbs =
            get_wkb_line(m_buffer.get<osmium::WayNodeList>(0), split_at);
        std::move(linewkbs.begin(), linewkbs.end(),
                  std::inserter(ret, ret.end()));
    }

    if (done_ways < todo_ways) {
        // oh dear, there must be circular ways without an end
        // need to do the same shebang again
        for (size_t i = 0; i < todo_ways; ++i) {
            if (!std::get<1>(conns[i])) {
                continue; // way already done
            }

            m_buffer.clear();

            {
                osmium::builder::WayNodeListBuilder wnl_builder{m_buffer};
                size_t prev = std::get<0>(conns[i]);
                size_t cur = i;
                bool skip_first = false;

                do {
                    auto &conn = conns[cur];
                    assert(std::get<1>(conn));
                    auto const &nl = std::get<1>(conn)->nodes();
                    bool const forward = std::get<0>(conn) == prev;
                    prev = cur;
                    if (forward) {
                        // add way forwards
                        add_nodes_to_builder(wnl_builder, nl.cbegin(),
                                             nl.cend(), skip_first);
                        cur = std::get<2>(conn);
                    } else {
                        // add way backwards
                        add_nodes_to_builder(wnl_builder, nl.crbegin(),
                                             nl.crend(), skip_first);
                        cur = std::get<0>(conn);
                    }
                    // mark way as done
                    std::get<1>(conns[prev]) = nullptr;
                    skip_first = true;
                } while (cur != i);
            }

            // found a line end, create the wkbs
            m_buffer.commit();
            auto linewkbs =
                get_wkb_line(m_buffer.get<osmium::WayNodeList>(0), split_at);
            std::move(linewkbs.begin(), linewkbs.end(),
                      std::inserter(ret, ret.end()));
        }
    }

    if (split_at <= 0.0 && !ret.empty()) {
        auto const num_lines = ret.size();
        m_writer.multilinestring_start();
        for (auto const &line : ret) {
            m_writer.add_sub_geometry(line);
        }
        ret.clear();
        ret.push_back(m_writer.multilinestring_finish(num_lines));
    }

    return ret;
}

size_t osmium_builder_t::add_mp_points(osmium::NodeRefList const &nodes)
{
    size_t num_points = 0;
    osmium::Location last_location;
    for (auto const &node_ref : nodes) {
        if (node_ref.location().valid() &&
            last_location != node_ref.location()) {
            last_location = node_ref.location();
            m_writer.add_location(m_proj->reproject(last_location));
            ++num_points;
        }
    }

    return num_points;
}

osmium_builder_t::wkb_t
osmium_builder_t::create_multipolygon(osmium::Area const &area)
{
    wkb_t ret;

    auto const polys = create_polygons(area);

    switch (polys.size()) {
    case 0:
        break; //nothing
    case 1:
        ret = polys[0];
        break;
    default:
        m_writer.multipolygon_start();
        for (auto const &p : polys) {
            m_writer.add_sub_geometry(p);
        }
        ret = m_writer.multipolygon_finish(polys.size());
        break;
    }

    return ret;
}

osmium_builder_t::wkbs_t
osmium_builder_t::create_polygons(osmium::Area const &area)
{
    wkbs_t ret;

    try {
        size_t num_rings = 0;

        for (auto const &item : area) {
            if (item.type() == osmium::item_type::outer_ring) {
                auto const &ring = static_cast<osmium::OuterRing const &>(item);
                if (num_rings > 0) {
                    ret.push_back(m_writer.polygon_finish(num_rings));
                    num_rings = 0;
                }
                m_writer.polygon_start();
                m_writer.polygon_ring_start();
                auto const num_points = add_mp_points(ring);
                m_writer.polygon_ring_finish(num_points);
                ++num_rings;
            } else if (item.type() == osmium::item_type::inner_ring) {
                auto const &ring = static_cast<osmium::InnerRing const &>(item);
                m_writer.polygon_ring_start();
                auto const num_points = add_mp_points(ring);
                m_writer.polygon_ring_finish(num_points);
                ++num_rings;
            }
        }

        auto const wkb = m_writer.polygon_finish(num_rings);
        if (num_rings > 0) {
            ret.push_back(wkb);
        }

    } catch (osmium::geometry_error const &) { /* ignored */
    }

    return ret;
}

} // namespace geom
