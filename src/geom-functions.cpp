/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-functions.hpp"
#include "geom-boost-adaptor.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
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

/****************************************************************************/

std::string_view geometry_type(geometry_t const &geom)
{
    using namespace std::literals::string_view_literals;
    return geom.visit(overloaded{
        [&](geom::nullgeom_t const & /*input*/) { return "NULL"sv; },
        [&](geom::point_t const & /*input*/) { return "POINT"sv; },
        [&](geom::linestring_t const & /*input*/) { return "LINESTRING"sv; },
        [&](geom::polygon_t const & /*input*/) { return "POLYGON"sv; },
        [&](geom::multipoint_t const & /*input*/) { return "MULTIPOINT"sv; },
        [&](geom::multilinestring_t const & /*input*/) {
            return "MULTILINESTRING"sv;
        },
        [&](geom::multipolygon_t const & /*input*/) {
            return "MULTIPOLYGON"sv;
        },
        [&](geom::collection_t const & /*input*/) {
            return "GEOMETRYCOLLECTION"sv;
        }});
}

/****************************************************************************/

std::size_t num_geometries(geometry_t const &geom)
{
    return geom.visit(
        [&](auto const &input) { return input.num_geometries(); });
}

/****************************************************************************/

namespace {

class geometry_n_visitor
{
public:
    geometry_n_visitor(geometry_t *output, std::size_t n)
    : m_output(output), m_n(n)
    {}

    void operator()(geom::collection_t const &input) const
    {
        *m_output = input[m_n];
    }

    template <typename T>
    void operator()(geom::multigeometry_t<T> const &input) const
    {
        m_output->set<T>() = input[m_n];
    }

    template <typename T>
    void operator()(T const &input) const
    {
        m_output->set<T>() = input;
    }

private:
    geometry_t *m_output;
    std::size_t m_n;

}; // class geometry_n_visitor

} // anonymous namespace

void geometry_n(geometry_t *output, geometry_t const &input, std::size_t n)
{
    auto const max = num_geometries(input);
    if (n < 1 || n > max) {
        output->reset();
        return;
    }

    input.visit(geometry_n_visitor{output, n - 1});
    output->set_srid(input.srid());
}

geometry_t geometry_n(geometry_t const &input, std::size_t n)
{
    geom::geometry_t output{};
    geometry_n(&output, input, n);
    return output;
}

/****************************************************************************/

namespace {

void set_to_same_type(geometry_t *output, geometry_t const &input)
{
    input.visit([&](auto in) { output->set<decltype(in)>(); });
}

class transform_visitor
{
public:
    explicit transform_visitor(geometry_t *output,
                               reprojection const *reprojection)
    : m_output(output), m_reprojection(reprojection)
    {}

    void operator()(nullgeom_t const & /*input*/) const {}

    void operator()(point_t const &input) const
    {
        m_output->get<point_t>() = project(input);
    }

    void operator()(linestring_t const &input) const
    {
        transform_points(&m_output->get<linestring_t>(), input);
    }

    void operator()(polygon_t const &input) const
    {
        transform_polygon(&m_output->get<polygon_t>(), input);
    }

    void operator()(multipoint_t const &input) const
    {
        auto &m = m_output->get<multipoint_t>();
        m.reserve(input.num_geometries());
        for (auto const point : input) {
            m.add_geometry(project(point));
        }
    }

    void operator()(multilinestring_t const &input) const
    {
        auto &m = m_output->set<multilinestring_t>();
        m.reserve(input.num_geometries());
        for (auto const &line : input) {
            transform_points(&m.add_geometry(), line);
        }
    }

    void operator()(multipolygon_t const &input) const
    {
        auto &m = m_output->set<multipolygon_t>();
        m.reserve(input.num_geometries());
        for (auto const &polygon : input) {
            transform_polygon(&m.add_geometry(), polygon);
        }
    }

    void operator()(collection_t const &input) const
    {
        auto &m = m_output->get<collection_t>();
        m.reserve(input.num_geometries());
        for (auto const &geom : input) {
            auto &new_geom = m.add_geometry();
            set_to_same_type(&new_geom, geom);
            new_geom.set_srid(0);
            geom.visit(transform_visitor{&new_geom, m_reprojection});
        }
    }

private:
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

    geometry_t *m_output;
    reprojection const *m_reprojection;

}; // class transform_visitor

} // anonymous namespace

void transform(geometry_t *output, geometry_t const &input,
               reprojection const &reprojection)
{
    assert(input.srid() == 4326);

    set_to_same_type(output, input);
    output->set_srid(reprojection.target_srs());
    input.visit(transform_visitor{output, &reprojection});
}

geometry_t transform(geometry_t const &input, reprojection const &reprojection)
{
    geometry_t output;
    transform(&output, input, reprojection);
    return output;
}

/****************************************************************************/

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
        output->remove_last();
    }
}

void segmentize(geometry_t *output, geometry_t const &input,
                double max_segment_length)
{
    output->set_srid(input.srid());
    auto *multilinestring = &output->set<multilinestring_t>();

    if (input.is_linestring()) {
        split_linestring(input.get<linestring_t>(), max_segment_length,
                         multilinestring);
    } else if (input.is_multilinestring()) {
        for (auto const &line : input.get<multilinestring_t>()) {
            split_linestring(line, max_segment_length, multilinestring);
        }
    } else {
        output->reset();
    }
}

geometry_t segmentize(geometry_t const &input, double max_segment_length)
{
    geometry_t output;
    segmentize(&output, input, max_segment_length);
    return output;
}

/****************************************************************************/

double area(geometry_t const &geom)
{
    return std::abs(geom.visit(
        overloaded{[&](geom::nullgeom_t const & /*input*/) { return 0.0; },
                   [&](geom::collection_t const &input) {
                       return std::accumulate(input.cbegin(), input.cend(), 0.0,
                                              [](double sum, auto const &geom) {
                                                  return sum + area(geom);
                                              });
                   },
                   [&](auto const &input) {
                       return static_cast<double>(boost::geometry::area(input));
                   }}));
}

/****************************************************************************/

double length(geometry_t const &geom)
{
    return geom.visit(overloaded{
        [&](geom::nullgeom_t const & /*input*/) { return 0.0; },
        [&](geom::collection_t const &input) {
            double total = 0.0;
            for (auto const &item : input) {
                total += length(item);
            }
            return total;
        },
        [&](auto const &input) {
            return static_cast<double>(boost::geometry::length(input));
        }});
}

/****************************************************************************/

namespace {

class split_visitor
{
public:
    split_visitor(std::vector<geometry_t> *output, int srid) noexcept
    : m_output(output), m_srid(srid)
    {}

    template <typename T>
    void operator()(T) const
    {}

    void operator()(geom::collection_t &&geom) const
    {
        for (auto &&sgeom : geom) {
            m_output->push_back(std::move(sgeom));
        }
    }

    template <typename T>
    void operator()(geom::multigeometry_t<T> &&geom) const
    {
        for (auto &&sgeom : geom) {
            m_output->emplace_back(std::move(sgeom), m_srid);
        }
    }

private:
    std::vector<geometry_t> *m_output;
    int m_srid;

}; // class split_visitor

} // anonymous namespace

std::vector<geometry_t> split_multi(geometry_t &&geom, bool split_multi)
{
    std::vector<geometry_t> output;

    if (split_multi && geom.is_multi()) {
        visit(split_visitor{&output, geom.srid()}, std::move(geom));
    } else if (!geom.is_null()) {
        output.push_back(std::move(geom));
    }

    return output;
}

/****************************************************************************/

static void reverse(geom::nullgeom_t * /*output*/,
                    geom::nullgeom_t const & /*input*/) noexcept
{}

static void reverse(geom::point_t *output, geom::point_t const &input) noexcept
{
    *output = input;
}

static void reverse(point_list_t *output, point_list_t const &input)
{
    output->reserve(input.size());
    std::reverse_copy(input.cbegin(), input.cend(),
                      std::back_inserter(*output));
}

static void reverse(geom::polygon_t *output, geom::polygon_t const &input)
{
    reverse(&output->outer(), input.outer());
    for (auto const &g : input.inners()) {
        reverse(&output->inners().emplace_back(), g);
    }
}

template <typename T>
void reverse(geom::multigeometry_t<T> *output,
             geom::multigeometry_t<T> const &input)
{
    output->reserve(input.num_geometries());
    for (auto const &g : input) {
        reverse(&output->add_geometry(), g);
    }
}

void reverse(geometry_t *output, geometry_t const &input)
{
    output->set_srid(input.srid());

    input.visit([&](auto const &geom) {
        using inner_type =
            std::remove_const_t<std::remove_reference_t<decltype(geom)>>;
        return reverse(&output->set<inner_type>(), geom);
    });
}

geometry_t reverse(geometry_t const &input)
{
    geometry_t output;
    reverse(&output, input);
    return output;
}

/****************************************************************************/

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

void line_merge(geometry_t *output, geometry_t const &input)
{
    if (input.is_linestring()) {
        *output = input;
        return;
    }

    if (!input.is_multilinestring()) {
        output->reset();
        return;
    }

    output->set_srid(input.srid());

    auto &linestrings = output->set<multilinestring_t>();

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
    constexpr auto const NOCONN = std::numeric_limits<std::size_t>::max();

    struct connection_t
    {
        std::size_t left = NOCONN;
        linestring_t const *ls;
        std::size_t right = NOCONN;

        explicit connection_t(linestring_t const *l) noexcept : ls(l) {}
    };

    std::vector<connection_t> conns;

    // Initialize the two lists.
    for (auto const &line : input.get<multilinestring_t>()) {
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
        output->reset();
    }
}

geometry_t line_merge(geometry_t const &input)
{
    geometry_t output;
    line_merge(&output, input);
    return output;
}

/****************************************************************************/

/**
 * This helper function is used to calculate centroids of geometry collections.
 * It first creates a multi geometry that only contains the geometries of
 * dimension N from the input collection. This is done by copying the geometry,
 * which isn't very efficient, but hopefully the centroid of a geometry
 * collection isn't used very often. This can be optimized if needed.
 *
 * Then the centroid of this new collection is calculated.
 *
 * Nested geometry collections are not allowed.
 */
template <std::size_t N, typename T>
static void filtered_centroid(collection_t const &collection, point_t *center)
{
    multigeometry_t<T> multi;
    for (auto const &geom : collection) {
        assert(!geom.is_collection());
        if (!geom.is_null() && dimension(geom) == N) {
            if (geom.is_multi()) {
                for (auto const &sgeom : geom.get<multigeometry_t<T>>()) {
                    multi.add_geometry() = sgeom;
                }
            } else {
                multi.add_geometry() = geom.get<T>();
            }
        }
    }
    boost::geometry::centroid(multi, *center);
}

geometry_t centroid(geometry_t const &geom)
{
    geom::geometry_t output{point_t{}, geom.srid()};
    auto &center = output.get<point_t>();

    geom.visit(overloaded{
        [&](geom::nullgeom_t const & /*input*/) { output.reset(); },
        [&](geom::collection_t const &input) {
            switch (dimension(input)) {
            case 0:
                filtered_centroid<0, point_t>(input, &center);
                break;
            case 1:
                filtered_centroid<1, linestring_t>(input, &center);
                break;
            default: // 2
                filtered_centroid<2, polygon_t>(input, &center);
                break;
            }
        },
        [&](auto const &input) { boost::geometry::centroid(input, center); }});

    return output;
}

/****************************************************************************/

static bool simplify(linestring_t *output, linestring_t const &input,
                     double tolerance)
{
    boost::geometry::simplify(input, *output, tolerance);

    // Linestrings with less then 2 points are invalid. Older boost::geometry
    // versions will generate a "line" with two identical points. We are
    // paranoid here and remove all duplicate points and then check that we
    // have at least 2 points.
    output->remove_duplicates();
    return output->size() > 1;
}

static bool simplify(multilinestring_t *output, multilinestring_t const &input,
                     double tolerance)
{
    for (auto const &ls : input) {
        linestring_t simplified_ls;
        if (simplify(&simplified_ls, ls, tolerance)) {
            output->add_geometry(std::move(simplified_ls));
        }
    }
    return output->num_geometries() > 0;
}

template <typename T>
static bool simplify(T * /*output*/, T const & /*input*/, double /*tolerance*/)
{
    return false;
}

void simplify(geometry_t *output, geometry_t const &input, double tolerance)
{
    output->set_srid(input.srid());

    input.visit([&](auto const &input) {
        using inner_type =
            std::remove_const_t<std::remove_reference_t<decltype(input)>>;
        auto &out = output->set<inner_type>();

        if (!simplify(&out, input, tolerance)) {
            output->reset();
        }
    });
}

geometry_t simplify(geometry_t const &input, double tolerance)
{
    geom::geometry_t output{linestring_t{}, input.srid()};
    simplify(&output, input, tolerance);
    return output;
}

} // namespace geom
