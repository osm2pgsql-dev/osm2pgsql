/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-box.hpp"

namespace geom {

box_t &box_t::extend(point_t const &point) noexcept
{
    if (point.x() < m_min_x) {
        m_min_x = point.x();
    }
    if (point.y() < m_min_y) {
        m_min_y = point.y();
    }
    if (point.x() > m_max_x) {
        m_max_x = point.x();
    }
    if (point.y() > m_max_y) {
        m_max_y = point.y();
    }

    return *this;
}

void box_t::extend(point_list_t const &list) noexcept
{
    for (auto const &point : list) {
        extend(point);
    }
}

namespace {

class envelope_visitor
{
public:
    box_t operator()(geom::nullgeom_t const & /*geom*/) const
    {
        return box_t{};
    }

    box_t operator()(geom::point_t const &geom) const
    {
        box_t box;
        box.extend(geom);
        return box;
    }

    box_t operator()(geom::linestring_t const &geom) const
    {
        box_t box;
        box.extend(geom);
        return box;
    }

    box_t operator()(geom::polygon_t const &geom) const
    {
        box_t box;
        box.extend(geom.outer());
        return box;
    }

    box_t operator()(geom::multipoint_t const & /*geom*/) const
    {
        assert(false);
        return {}; // XXX not implemented
    }

    box_t operator()(geom::multilinestring_t const &geom) const
    {
        box_t box;

        for (auto const &line : geom) {
            box.extend(line);
        }

        return box;
    }

    box_t operator()(geom::multipolygon_t const &geom) const
    {
        box_t box;

        for (auto const &polygon : geom) {
            box.extend(polygon.outer());
        }

        return box;
    }

    box_t operator()(geom::collection_t const & /*geom*/) const
    {
        assert(false);
        return {}; // XXX not implemented
    }
}; // class envelope_visitor

} // anonymous namespace

box_t envelope(geometry_t const &geom)
{
    return geom.visit(envelope_visitor{});
}

} // namespace geom
