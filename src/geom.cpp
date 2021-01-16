/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include <algorithm>
#include <iterator>

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

    for (auto const this_pt : line) {
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
                } else {
                    dist = distance(this_pt, ipoint);
                }
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

void make_line(osmium::NodeRefList const &nodes, reprojection const &proj,
               double split_at, std::vector<linestring_t> *out)
{
    linestring_t line{nodes, proj};

    if (line.empty()) {
        return;
    }

    if (split_at > 0.0) {
        split_linestring(line, split_at, out);
    } else {
        out->emplace_back(std::move(line));
    }
}

} // namespace geom

