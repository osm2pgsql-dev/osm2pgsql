/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-pole-of-inaccessibility.hpp"
#include "geom-boost-adaptor.hpp"
#include "geom-box.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <queue>

/**
 * \file
 *
 * Implementation of the "Polylabel" algorithm for finding the "pole of
 * inaccessibility", the internal point most distant from the polygon outline,
 * or center of the maximum inscribed circle, described in
 * https://blog.mapbox.com/a-new-algorithm-for-finding-a-visual-center-of-a-polygon-7c77e6492fbc
 *
 * Adapted from https://github.com/mapbox/polylabel and
 * https://github.com/libgeos/geos/blob/main/src/algorithm/construct/MaximumInscribedCircle.cpp
 * including the change from https://github.com/mapbox/polylabel/issues/82 .
 *
 * Forcing the precision to be no smaller than max(width, height) / 1000 of the
 * envelope makes sure the algorithm terminates in sensible run-time and without
 * taking too much memory. The value of 1000.0 was taken from the PostGIS
 * implementation, but unlike the PostGIS implementation you can set a higher
 * value.
 */

namespace geom {

/// Get squared distance from a point p to a segment (a, b).
static double point_to_segment_distance_squared(point_t p, point_t a, point_t b,
                                                double stretch) noexcept
{
    double x = a.x();
    double y = a.y() * stretch;
    double dx = b.x() - x;
    double dy = b.y() * stretch - y;

    if (dx != 0 || dy != 0) {
        double const t =
            ((p.x() - x) * dx + (p.y() - y) * dy) / (dx * dx + dy * dy);

        if (t > 1) {
            x = b.x();
            y = b.y() * stretch;
        } else if (t > 0) {
            x += dx * t;
            y += dy * t;
        }
    }

    dx = p.x() - x;
    dy = p.y() - y;

    return dx * dx + dy * dy;
}

/// Get squared distance from a point p to ring.
static bool point_to_ring_distance_squared(point_t point, ring_t const &ring,
                                           bool inside, double stretch,
                                           double *min_dist_squared) noexcept
{
    std::size_t const len = ring.size();

    for (std::size_t i = 0, j = len - 1; i < len; j = i++) {
        auto const &a = ring[i];
        auto const &b = ring[j];

        if (((a.y() * stretch) > point.y()) !=
                ((b.y() * stretch) > point.y()) &&
            (point.x() < (b.x() - a.x()) * (point.y() - (a.y() * stretch)) /
                                 ((b.y() - a.y()) * stretch) +
                             a.x())) {
            inside = !inside;
        }

        double const d =
            point_to_segment_distance_squared(point, a, b, stretch);
        if (d < *min_dist_squared) {
            *min_dist_squared = d;
        }
    }

    return inside;
}

/**
 * Signed distance from point to polygon boundary. The result is negative if
 * the point is outside.
 */
static auto point_to_polygon_distance(point_t point, polygon_t const &polygon,
                                      double stretch)
{
    double min_dist_squared = std::numeric_limits<double>::infinity();

    bool inside = point_to_ring_distance_squared(point, polygon.outer(), false,
                                                 stretch, &min_dist_squared);

    for (auto const &ring : polygon.inners()) {
        inside = point_to_ring_distance_squared(point, ring, inside, stretch,
                                                &min_dist_squared);
    }

    return (inside ? 1 : -1) * std::sqrt(min_dist_squared);
}

namespace {

struct Cell
{
    static constexpr double const SQRT2 = 1.4142135623730951;

    Cell(point_t c, double h, polygon_t const &polygon, double stretch)
    : center(c), half_size(h),
      dist(point_to_polygon_distance(center, polygon, stretch)),
      max(dist + half_size * SQRT2)
    {}

    point_t center;   // cell center
    double half_size; // half the cell size
    double dist;      // distance from cell center to polygon
    double max;       // max distance to polygon within a cell

    friend bool operator<(Cell const &a, Cell const &b) noexcept
    {
        return a.max < b.max;
    }
};

} // anonymous namespace

static Cell make_centroid_cell(polygon_t const &polygon, double stretch)
{
    point_t centroid{0, 0};
    boost::geometry::centroid(polygon, centroid);
    centroid.set_y(stretch * centroid.y());
    return {centroid, 0, polygon, stretch};
}

point_t pole_of_inaccessibility(const polygon_t &polygon, double precision,
                                double stretch)
{
    assert(stretch > 0);

    box_t const envelope = geom::envelope(polygon);

    double const min_precision =
        std::max(envelope.width(), envelope.height()) / 1000.0;
    if (min_precision > precision) {
        precision = min_precision;
    }

    box_t const stretched_envelope{envelope.min_x(), envelope.min_y() * stretch,
                                   envelope.max_x(),
                                   envelope.max_y() * stretch};

    if (stretched_envelope.width() == 0 || stretched_envelope.height() == 0) {
        return envelope.min();
    }

    std::priority_queue<Cell, std::vector<Cell>> cell_queue;

    // cover polygon with initial cells
    if (stretched_envelope.width() == stretched_envelope.height()) {
        double const cell_size = stretched_envelope.width();
        double const h = cell_size / 2.0;
        cell_queue.emplace(stretched_envelope.center(), h, polygon, stretch);
    } else if (stretched_envelope.width() < stretched_envelope.height()) {
        double const cell_size = stretched_envelope.width();
        double const h = cell_size / 2.0;
        int const count = static_cast<int>(std::ceil(
            stretched_envelope.height() / stretched_envelope.width()));
        for (int n = 0; n < count; ++n) {
            cell_queue.emplace(
                point_t{stretched_envelope.center().x(),
                        stretched_envelope.min().y() + n * cell_size + h},
                h, polygon, stretch);
        }
    } else {
        double const cell_size = stretched_envelope.height();
        double const h = cell_size / 2.0;
        int const count = static_cast<int>(std::ceil(
            stretched_envelope.width() / stretched_envelope.height()));
        for (int n = 0; n < count; ++n) {
            cell_queue.emplace(
                point_t{stretched_envelope.min().x() + n * cell_size + h,
                        stretched_envelope.center().y()},
                h, polygon, stretch);
        }
    }

    // take centroid as the first best guess
    auto best_cell = make_centroid_cell(polygon, stretch);

    // second guess: bounding box centroid
    Cell const bbox_cell{stretched_envelope.center(), 0, polygon, stretch};
    if (bbox_cell.dist > best_cell.dist) {
        best_cell = bbox_cell;
    }

    auto num_probes = cell_queue.size();
    while (!cell_queue.empty()) {
        // pick the most promising cell from the queue
        auto cell = cell_queue.top();
        cell_queue.pop();

        // update the best cell if we found a better one
        if (cell.dist > best_cell.dist) {
            best_cell = cell;
            log_debug("polyline: found best {} after {} probes",
                      ::round(1e4 * cell.dist) / 1e4, num_probes);
        }

        // do not drill down further if there's no chance of a better solution
        if (cell.max - best_cell.dist <= precision) {
            continue;
        }

        // split the cell into four cells
        auto const h = cell.half_size / 2.0;
        auto const center = cell.center;

        for (auto dy : {-h, h}) {
            for (auto dx : {-h, h}) {
                Cell const c{point_t{center.x() + dx, center.y() + dy}, h,
                             polygon, stretch};
                if (c.max > best_cell.dist) {
                    cell_queue.push(c);
                }
            }
        }

        num_probes += 4;
    }

    log_debug("polyline: num probes: {}", num_probes);
    log_debug("polyline: best distance: {}", best_cell.dist);

    return {best_cell.center.x(), best_cell.center.y() / stretch};
}

void pole_of_inaccessibility(geometry_t *output, geometry_t const &input,
                             double precision, double stretch)
{
    if (input.is_polygon()) {
        output->set<geom::point_t>() = pole_of_inaccessibility(
            input.get<geom::polygon_t>(), precision, stretch);
        output->set_srid(input.srid());
    } else {
        output->reset();
    }
}

geometry_t pole_of_inaccessibility(geometry_t const &input, double precision,
                                   double stretch)
{
    geometry_t geom;
    pole_of_inaccessibility(&geom, input, precision, stretch);
    return geom;
}

} // namespace geom
