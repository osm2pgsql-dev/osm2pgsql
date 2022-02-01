/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "wkb.hpp"

namespace ewkb {

template <typename T>
static void str_push(std::string *data, T value)
{
    data->append(reinterpret_cast<char const *const>(&value), sizeof(T));
}

/**
 * Add a EWKB header without length field to the string.
 *
 * This header is always 1 + 4 + 4 = 9 bytes long
 *
 * \pre \code data != nullptr \endcode
 */
static void write_header(std::string *data, geometry_type type, uint32_t srid)
{
    str_push(data, Endian);
    if (srid) {
        str_push(data, type | geometry_type::wkb_srid);
        str_push(data, srid);
    } else {
        str_push(data, type);
    }
}

/**
 * Add a EWKB 32bit unsigned int length field to the string.
 *
 * This header is always 4 bytes long
 *
 * \pre \code data != nullptr \endcode
 */
static void write_length(std::string *data, std::size_t length)
{
    str_push(data, static_cast<uint32_t>(length));
}

static void write_points(std::string *data, geom::point_list_t const &points)
{
    write_length(data, points.size());
    for (auto const &point : points) {
        str_push(data, point.x());
        str_push(data, point.y());
    }
}

static void write_linestring(std::string *data, geom::linestring_t const &geom,
                             uint32_t srid)
{
    assert(data);

    write_header(data, wkb_line, srid);
    write_points(data, geom);
}

static void write_polygon(std::string *data, geom::polygon_t const &geom,
                          uint32_t srid)
{
    assert(data);

    write_header(data, wkb_polygon, srid);
    write_length(data, geom.inners().size() + 1);
    write_points(data, geom.outer());
    for (auto const &ring : geom.inners()) {
        write_points(data, ring);
    }
}

namespace {

class make_ewkb_visitor
{
public:
    make_ewkb_visitor(uint32_t srid, bool ensure_multi) noexcept
    : m_srid(srid), m_ensure_multi(ensure_multi)
    {}

    std::string operator()(geom::nullgeom_t const & /*geom*/) const
    {
        return {};
    }

    std::string operator()(geom::point_t const &geom) const
    {
        // 9 byte header plus one set of coordinates
        constexpr const std::size_t size = 9 + 2 * 8;

        std::string data;

        data.reserve(size);
        write_header(&data, wkb_point, m_srid);
        str_push(&data, geom.x());
        str_push(&data, geom.y());

        assert(data.size() == size);

        return data;
    }

    std::string operator()(geom::linestring_t const &geom) const
    {
        std::string data;

        if (m_ensure_multi) {
            // Two 13 bytes headers plus n sets of coordinates
            data.reserve(2 * 13 + geom.size() * (2 * 8));
            write_header(&data, wkb_multi_line, m_srid);
            write_length(&data, 1);
        } else {
            // 13 byte header plus n sets of coordinates
            data.reserve(13 + geom.size() * (2 * 8));
        }
        write_linestring(&data, geom, m_srid);

        return data;
    }

    std::string operator()(geom::polygon_t const &geom) const
    {
        std::string data;

        if (m_ensure_multi) {
            write_header(&data, wkb_multi_polygon, m_srid);
            write_length(&data, 1);
        }
        write_polygon(&data, geom, m_srid);

        return data;
    }

    std::string operator()(geom::multipoint_t const & /*geom*/) const
    {
        assert(false);
        return {}; // XXX not used yet, no implementation
    }

    std::string operator()(geom::multilinestring_t const &geom) const
    {
        std::string data;

        write_header(&data, wkb_multi_line, m_srid);
        write_length(&data, geom.num_geometries());

        for (auto const &line : geom) {
            write_linestring(&data, line, 0);
        }

        return data;
    }

    std::string operator()(geom::multipolygon_t const &geom) const
    {
        std::string data;

        write_header(&data, wkb_multi_polygon, m_srid);
        write_length(&data, geom.num_geometries());

        for (auto const &polygon : geom) {
            write_polygon(&data, polygon, 0);
        }

        return data;
    }

    std::string operator()(geom::collection_t const & /*geom*/) const
    {
        assert(false);
        return {}; // XXX not used yet, no implementation
    }

private:
    uint32_t m_srid;
    bool m_ensure_multi;

}; // class make_ewkb_visitor

} // anonymous namespace

} // namespace ewkb

std::string geom_to_ewkb(geom::geometry_t const &geom, bool ensure_multi)
{
    return geom.visit(ewkb::make_ewkb_visitor{
        static_cast<uint32_t>(geom.srid()), ensure_multi});
}
