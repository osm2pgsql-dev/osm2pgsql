/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-functions.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <tuple>
#include <utility>

namespace geom {

double distance(point_t p1, point_t p2) noexcept
{
    double const dx = p1.x() - p2.x();
    double const dy = p1.y() - p2.y();
    return std::sqrt(dx * dx + dy * dy);
}

point_t interpolate(point_t p1, point_t p2, double frac) noexcept
{
    return point_t{frac * (p1.x() - p2.x()) + p2.x(),
                   frac * (p1.y() - p2.y()) + p2.y()};
}

namespace {

class transform_visitor
{
public:
    explicit transform_visitor(reprojection const *reprojection)
    : m_reprojection(reprojection)
    {}

    geometry_t operator()(geom::nullgeom_t const & /*geom*/) const
    {
        return {};
    }

    geometry_t operator()(geom::point_t const &geom) const
    {
        return geometry_t{project(geom), srid()};
    }

    geometry_t operator()(geom::linestring_t const &geom) const
    {
        geometry_t output{linestring_t{}, srid()};
        transform_points(&output.get<linestring_t>(), geom);
        return output;
    }

    geometry_t operator()(geom::polygon_t const &geom) const
    {
        geometry_t output{polygon_t{}, srid()};
        transform_polygon(&output.get<polygon_t>(), geom);
        return output;
    }

    geometry_t operator()(geom::multipoint_t const & /*geom*/) const
    {
        assert(false);
        return {};
    }

    geometry_t operator()(geom::multilinestring_t const &geom) const
    {
        geometry_t output{multilinestring_t{}, srid()};

        for (auto const &line : geom) {
            transform_points(&output.get<multilinestring_t>().add_geometry(),
                             line);
        }

        return output;
    }

    geometry_t operator()(geom::multipolygon_t const &geom) const
    {
        geometry_t output{multipolygon_t{}, srid()};

        for (auto const &polygon : geom) {
            transform_polygon(&output.get<multipolygon_t>().add_geometry(),
                              polygon);
        }

        return output;
    }

    geometry_t operator()(geom::collection_t const & /*geom*/) const
    {
        return {}; // XXX not implemented
    }

private:
    int srid() const noexcept { return m_reprojection->target_srs(); }

    point_t project(point_t point) const
    {
        return m_reprojection->reproject(point);
    }

    void transform_points(point_list_t *output, point_list_t const &input) const
    {
        output->reserve(input.size());
        for (auto const &point : input) {
            output->push_back(project(point));
        }
    }

    void transform_polygon(polygon_t *output, polygon_t const &input) const
    {
        transform_points(&output->outer(), input.outer());

        output->inners().reserve(input.inners().size());
        for (auto const &inner : input.inners()) {
            auto &oring = output->inners().emplace_back();
            transform_points(&oring, inner);
        }
    }

    reprojection const *m_reprojection;

}; // class transform_visitor

} // anonymous namespace

geometry_t transform(geometry_t const &geom, reprojection const &reprojection)
{
    assert(geom.srid() == 4326);
    return geom.visit(transform_visitor{&reprojection});
}

namespace {

/**
 * Helper class for iterating over all points except the first one in a point
 * list.
 */
class without_first
{
public:
    explicit without_first(point_list_t const &list) : m_list(list) {}

    point_list_t::const_iterator begin()
    {
        assert(m_list.begin() != m_list.end());
        return std::next(m_list.begin());
    }

    point_list_t::const_iterator end() { return m_list.end(); }

private:
    point_list_t const &m_list;
}; // class without_first

} // anonymous namespace

static void split_linestring(linestring_t const &line, double split_at,
                             multilinestring_t *output)
{
    double dist = 0;
    point_t prev_pt{line.front()};
    linestring_t *out = &output->add_geometry();
    out->push_back(prev_pt);

    for (auto const &this_pt : without_first(line)) {
        double const delta = distance(prev_pt, this_pt);

        // figure out if the addition of this point would take the total
        // length of the line in `segment` over the `split_at` distance.

        if (dist + delta > split_at) {
            auto const splits = (size_t)std::floor((dist + delta) / split_at);
            // use the splitting distance to split the current segment up
            // into as many parts as necessary to keep each part below
            // the `split_at` distance.
            point_t ipoint;
            for (size_t j = 0; j < splits; ++j) {
                double const frac = ((double)(j + 1) * split_at - dist) / delta;
                ipoint = interpolate(this_pt, prev_pt, frac);
                if (frac != 0.0) {
                    out->emplace_back(ipoint);
                }
                // start a new segment
                out = &output->add_geometry();
                out->emplace_back(ipoint);
            }
            // reset the distance based on the final splitting point for
            // the next iteration.
            if (this_pt == ipoint) {
                dist = 0;
                prev_pt = this_pt;
                continue;
            }
            dist = distance(this_pt, ipoint);
        } else {
            dist += delta;
        }

        out->push_back(this_pt);

        prev_pt = this_pt;
    }

    if (out->size() <= 1) {
        output->resize(output->num_geometries() - 1);
    }
}

geometry_t segmentize(geometry_t const &geom, double max_segment_length)
{
    geometry_t output{multilinestring_t{}, geom.srid()};
    auto *multilinestring = &output.get<multilinestring_t>();

    if (geom.is_linestring()) {
        split_linestring(geom.get<linestring_t>(), max_segment_length,
                         multilinestring);
    } else if (geom.is_multilinestring()) {
        for (auto const &line : geom.get<multilinestring_t>()) {
            split_linestring(line, max_segment_length, multilinestring);
        }
    } else {
        output.reset();
    }

    return output;
}

static double get_ring_area(ring_t const &ring) noexcept
{
    assert(ring.size() > 3);

    double total = 0.0;
    auto it = ring.begin();
    auto prev = *it++;

    while (it != ring.end()) {
        auto const cur = *it;
        total += prev.x() * cur.y() - cur.x() * prev.y();
        prev = cur;
        ++it;
    }

    return total;
}

static double get_polygon_area(polygon_t const &polygon)
{
    double total = get_ring_area(polygon.outer());

    for (auto const &ring : polygon.inners()) {
        total += get_ring_area(ring);
    }

    return total * 0.5;
}

double area(geometry_t const &geom)
{
    double total = 0.0;

    if (geom.is_polygon()) {
        total = get_polygon_area(geom.get<polygon_t>());
    } else if (geom.is_multipolygon()) {
        for (auto const &polygon : geom.get<multipolygon_t>()) {
            total += get_polygon_area(polygon);
        }
    }

    return total;
}

namespace {

class split_visitor
{
public:
    split_visitor(std::vector<geometry_t> *output, uint32_t srid) noexcept
    : m_output(output), m_srid(srid)
    {}

    template <typename T>
    void operator()(T) const
    {}

    void operator()(geom::collection_t const &geom) const
    {
        for (auto sgeom : geom) {
            m_output->push_back(std::move(sgeom));
        }
    }

    template <typename T>
    void operator()(geom::multigeometry_t<T> const &geom) const
    {
        for (auto sgeom : geom) {
            m_output->emplace_back(std::move(sgeom), m_srid);
        }
    }

private:
    std::vector<geometry_t> *m_output;
    uint32_t m_srid;

}; // class split_visitor

} // anonymous namespace

std::vector<geometry_t> split_multi(geometry_t geom, bool split_multi)
{
    std::vector<geometry_t> output;

    if (split_multi && geom.is_multi()) {
        geom.visit(split_visitor{&output, static_cast<uint32_t>(geom.srid())});
    } else if (!geom.is_null()) {
        output.push_back(std::move(geom));
    }

    return output;
}

/**
 * Add points specified by iterators to the linestring. If linestring is not
 * empty, do not add the first point returned by *it.
 */
template <typename ITERATOR>
static void add_nodes_to_linestring(linestring_t *linestring, ITERATOR it,
                                    ITERATOR end)
{
    if (!linestring->empty()) {
        assert(it != end);
        ++it;
    }

    while (it != end) {
        linestring->push_back(*it);
        ++it;
    }
}

geometry_t line_merge(geometry_t geom)
{
    geometry_t output{multilinestring_t{}, geom.srid()};

    if (geom.is_null()) {
        output.reset();
        return output;
    }

    assert(geom.is_multilinestring());

    // Make a list of all endpoints...
    struct endpoint_t
    {
        point_t c;
        std::size_t n;
        bool is_front;

        endpoint_t(point_t coords, std::size_t size, bool front) noexcept
        : c(coords), n(size), is_front(front)
        {}

        bool operator==(endpoint_t const &rhs) const noexcept
        {
            return c == rhs.c;
        }

        bool operator<(endpoint_t const &rhs) const noexcept
        {
            return std::tuple<double, double, std::size_t, bool>{c.x(), c.y(),
                                                                 n, is_front} <
                   std::tuple<double, double, std::size_t, bool>{
                       rhs.c.x(), rhs.c.y(), rhs.n, rhs.is_front};
        }
    };

    std::vector<endpoint_t> endpoints;

    // ...and a list of connections.
    constexpr std::size_t const NOCONN = -1UL;

    struct connection_t
    {
        std::size_t left = NOCONN;
        geom::linestring_t const *ls;
        std::size_t right = NOCONN;

        explicit connection_t(geom::linestring_t const *l) noexcept : ls(l) {}
    };

    std::vector<connection_t> conns;

    // Initialize the two lists.
    for (auto const &line : geom.get<multilinestring_t>()) {
        endpoints.emplace_back(line.front(), conns.size(), true);
        endpoints.emplace_back(line.back(), conns.size(), false);
        conns.emplace_back(&line);
    }

    std::sort(endpoints.begin(), endpoints.end());

    // Now fill the connection list based on the sorted enpoints list.
    for (auto it = std::adjacent_find(endpoints.cbegin(), endpoints.cend());
         it != endpoints.cend();
         it = std::adjacent_find(it + 2, endpoints.cend())) {
        auto const previd = it->n;
        auto const ptid = std::next(it)->n;
        if (it->is_front) {
            conns[previd].left = ptid;
        } else {
            conns[previd].right = ptid;
        }
        if (std::next(it)->is_front) {
            conns[ptid].left = previd;
        } else {
            conns[ptid].right = previd;
        }
    }

    auto &linestrings = output.get<multilinestring_t>();

    // First find all open ends and use them as starting points to assemble
    // linestrings. Mark ways as "done" as we go.
    std::size_t done_ways = 0;
    std::size_t const todo_ways = conns.size();
    for (std::size_t i = 0; i < todo_ways; ++i) {
        if (!conns[i].ls ||
            (conns[i].left != NOCONN && conns[i].right != NOCONN)) {
            continue; // way already done or not the beginning of a segment
        }

        linestring_t linestring;
        {
            std::size_t prev = NOCONN;
            std::size_t cur = i;

            do {
                auto &conn = conns[cur];
                assert(conn.ls);
                auto const &nl = *conn.ls;
                bool const forward = conn.left == prev;
                prev = cur;
                // add line
                if (forward) {
                    add_nodes_to_linestring(&linestring, nl.cbegin(),
                                            nl.cend());
                    cur = conn.right;
                } else {
                    add_nodes_to_linestring(&linestring, nl.crbegin(),
                                            nl.crend());
                    cur = conn.left;
                }
                // mark line as done
                conns[prev].ls = nullptr;
                ++done_ways;
            } while (cur != NOCONN);
        }

        // found a line end
        linestrings.add_geometry(std::move(linestring));
    }

    // If all ways have been "done", i.e. are part of a linestring now, we
    // are finished.
    if (done_ways < todo_ways) {
        // oh dear, there must be circular ways without an end
        // need to do the same shebang again
        for (std::size_t i = 0; i < todo_ways; ++i) {
            if (!conns[i].ls) {
                continue; // way already done
            }

            linestring_t linestring;
            {
                std::size_t prev = conns[i].left;
                std::size_t cur = i;

                do {
                    auto &conn = conns[cur];
                    assert(conn.ls);
                    auto const &nl = *conn.ls;
                    bool const forward =
                        (conn.left == prev &&
                         (!conns[conn.left].ls ||
                          conns[conn.left].ls->back() == nl.front()));
                    prev = cur;
                    if (forward) {
                        // add line forwards
                        add_nodes_to_linestring(&linestring, nl.cbegin(),
                                                nl.cend());
                        cur = conn.right;
                    } else {
                        // add line backwards
                        add_nodes_to_linestring(&linestring, nl.crbegin(),
                                                nl.crend());
                        cur = conn.left;
                    }
                    // mark line as done
                    conns[prev].ls = nullptr;
                } while (cur != i);
            }

            // found a line end
            linestrings.add_geometry(std::move(linestring));
        }
    }

    if (linestrings.num_geometries() == 0) {
        output.reset();
    }

    return output;
}

} // namespace geom
