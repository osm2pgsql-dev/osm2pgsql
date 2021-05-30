/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include "osmtypes.hpp"

#include <osmium/osm/way.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <tuple>

namespace geom {

double distance(osmium::geom::Coordinates p1,
                osmium::geom::Coordinates p2) noexcept
{
    double const dx = p1.x - p2.x;
    double const dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
}

osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                      osmium::geom::Coordinates p2,
                                      double frac) noexcept
{
    return osmium::geom::Coordinates{frac * (p1.x - p2.x) + p2.x,
                                     frac * (p1.y - p2.y) + p2.y};
}

linestring_t::linestring_t(
    std::initializer_list<osmium::geom::Coordinates> coords)
{
    std::copy(coords.begin(), coords.end(), std::back_inserter(m_coordinates));
}

linestring_t::linestring_t(osmium::NodeRefList const &nodes,
                           reprojection const &proj)
{
    osmium::Location last{};
    for (auto const &node : nodes) {
        auto const loc = node.location();
        if (loc.valid() && loc != last) {
            add_point(proj.reproject(loc));
            last = loc;
        }
    }

    if (size() <= 1) {
        m_coordinates.clear();
    }
}

void split_linestring(linestring_t const &line, double split_at,
                      std::vector<linestring_t> *out)
{
    double dist = 0;
    osmium::geom::Coordinates prev_pt{};
    out->emplace_back();

    for (auto const &this_pt : line) {
        if (prev_pt.valid()) {
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
                    if (frac != 0.0) {
                        out->back().add_point(ipoint);
                    }
                    // start a new segment
                    out->emplace_back();
                    out->back().add_point(ipoint);
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
        }

        out->back().add_point(this_pt);

        prev_pt = this_pt;
    }

    if (out->back().size() <= 1) {
        out->pop_back();
    }
}

void make_line(linestring_t &&line, double split_at,
               std::vector<linestring_t> *out)
{
    assert(out);

    if (line.empty()) {
        return;
    }

    if (split_at > 0.0) {
        split_linestring(line, split_at, out);
    } else {
        out->emplace_back(std::move(line));
    }
}

void make_multiline(osmium::memory::Buffer const &ways, double split_at,
                    reprojection const &proj, std::vector<linestring_t> *out)

{
    // make a list of all endpoints
    struct endpoint_t {
        osmid_t id;
        std::size_t n;
        bool is_front;

        endpoint_t(osmid_t ref, std::size_t size, bool front) noexcept
        : id(ref), n(size), is_front(front)
        {}

        bool operator==(endpoint_t const &rhs) const noexcept
        {
            return id == rhs.id;
        }
    };

    std::vector<endpoint_t> endpoints;

    // and a list of way connections
    enum lmt : size_t
    {
        NOCONN = -1UL
    };

    struct connection_t {
        std::size_t left = NOCONN;
        osmium::Way const *way;
        std::size_t right = NOCONN;

        explicit connection_t(osmium::Way const *w) noexcept : way(w) {}
    };

    std::vector<connection_t> conns;

    // initialise the two lists
    for (auto const &w : ways.select<osmium::Way>()) {
        if (w.nodes().size() > 1) {
            endpoints.emplace_back(w.nodes().front().ref(), conns.size(), true);
            endpoints.emplace_back(w.nodes().back().ref(), conns.size(), false);
            conns.emplace_back(&w);
        }
    }

    // sort by node id
    std::sort(endpoints.begin(), endpoints.end(), [
    ](endpoint_t const &a, endpoint_t const &b) noexcept {
        return std::tuple<osmid_t, std::size_t, bool>(a.id, a.n, a.is_front) <
               std::tuple<osmid_t, std::size_t, bool>(b.id, b.n, b.is_front);
    });

    // now fill the connection list based on the sorted list
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
        if (!conns[i].way ||
            (conns[i].left != NOCONN && conns[i].right != NOCONN)) {
            continue; // way already done or not the beginning of a segment
        }

        linestring_t linestring;
        {
            std::size_t prev = NOCONN;
            std::size_t cur = i;

            do {
                auto &conn = conns[cur];
                assert(conn.way);
                auto const &nl = conn.way->nodes();
                bool const forward = conn.left == prev;
                prev = cur;
                // add way nodes
                if (forward) {
                    add_nodes_to_linestring(linestring, proj, nl.cbegin(),
                                            nl.cend());
                    cur = conn.right;
                } else {
                    add_nodes_to_linestring(linestring, proj, nl.crbegin(),
                                            nl.crend());
                    cur = conn.left;
                }
                // mark way as done
                conns[prev].way = nullptr;
                ++done_ways;
            } while (cur != NOCONN);
        }

        // found a line end, create the wkbs
        make_line(std::move(linestring), split_at, out);
    }

    // If all ways have been "done", i.e. are part of a linestring now, we
    // are finished.
    if (done_ways >= todo_ways) {
        return;
    }

    // oh dear, there must be circular ways without an end
    // need to do the same shebang again
    for (size_t i = 0; i < todo_ways; ++i) {
        if (!conns[i].way) {
            continue; // way already done
        }

        linestring_t linestring;
        {
            size_t prev = conns[i].left;
            size_t cur = i;

            do {
                auto &conn = conns[cur];
                assert(conn.way);
                auto const &nl = conn.way->nodes();
                bool const forward =
                    (conn.left == prev &&
                     (!conns[conn.left].way ||
                      conns[conn.left].way->nodes().back() == nl.front()));
                prev = cur;
                if (forward) {
                    // add way forwards
                    add_nodes_to_linestring(linestring, proj, nl.cbegin(),
                                            nl.cend());
                    cur = conn.right;
                } else {
                    // add way backwards
                    add_nodes_to_linestring(linestring, proj, nl.crbegin(),
                                            nl.crend());
                    cur = conn.left;
                }
                // mark way as done
                conns[prev].way = nullptr;
            } while (cur != i);
        }

        // found a line end, create the wkbs
        make_line(std::move(linestring), split_at, out);
    }
}

} // namespace geom

