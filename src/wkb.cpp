/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "wkb.hpp"

#include "format.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ewkb {

namespace {

enum geometry_type : uint32_t
{
    wkb_point = 1,
    wkb_line = 2,
    wkb_polygon = 3,
    wkb_multi_point = 4,
    wkb_multi_line = 5,
    wkb_multi_polygon = 6,
    wkb_collection = 7,

    wkb_srid = 0x20000000 // SRID-presence flag (EWKB)
};

enum wkb_byte_order_type_t : uint8_t
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    Endian = 1 // Little Endian
#else
    Endian = 0, // Big Endian
#endif
};

template <typename T>
void str_push(std::string *data, T value)
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
void write_header(std::string *data, geometry_type type, uint32_t srid)
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
void write_length(std::string *data, std::size_t length)
{
    str_push(data, static_cast<uint32_t>(length));
}

void write_points(std::string *data, geom::point_list_t const &points)
{
    write_length(data, points.size());
    for (auto const &point : points) {
        str_push(data, point.x());
        str_push(data, point.y());
    }
}

void write_linestring(std::string *data, geom::linestring_t const &geom,
                             uint32_t srid)
{
    assert(data);

    write_header(data, wkb_line, srid);
    write_points(data, geom);
}

void write_polygon(std::string *data, geom::polygon_t const &geom,
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
            write_linestring(&data, geom, 0);
        } else {
            // 13 byte header plus n sets of coordinates
            data.reserve(13 + geom.size() * (2 * 8));
            write_linestring(&data, geom, m_srid);
        }

        return data;
    }

    std::string operator()(geom::polygon_t const &geom) const
    {
        std::string data;

        if (m_ensure_multi) {
            write_header(&data, wkb_multi_polygon, m_srid);
            write_length(&data, 1);
            write_polygon(&data, geom, 0);
        } else {
            write_polygon(&data, geom, m_srid);
        }

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

class ewkb_parser_t
{
public:
    explicit ewkb_parser_t(std::string const &wkb)
    : m_it(wkb.data()), m_end(wkb.data() + wkb.size()),
      m_max_length(wkb.size() / (sizeof(double) * 2))
    {}

    geom::geometry_t operator()()
    {
        geom::geometry_t geom;

        if (m_it == m_end) {
            return geom;
        }

        uint32_t const type = parse_header(&geom);

        switch (type) {
        case geometry_type::wkb_point:
            parse_point(&geom.set<geom::point_t>());
            break;
        case geometry_type::wkb_line:
            parse_point_list(&geom.set<geom::linestring_t>(), 2);
            break;
        case geometry_type::wkb_polygon:
            parse_polygon(&geom.set<geom::polygon_t>());
            break;
        case geometry_type::wkb_multi_point:
            // XXX not implemented yet
            break;
        case geometry_type::wkb_multi_line:
            parse_multi_linestring(&geom);
            break;
        case geometry_type::wkb_multi_polygon:
            parse_multi_polygon(&geom);
            break;
        default:
            throw std::runtime_error{
                "Invalid WKB geometry: Unknown geometry type {}"_format(type)};
        }

        if (m_it != m_end) {
            throw std::runtime_error{"Invalid WKB geometry: Extra data at end"};
        }

        return geom;
    }

private:
    void check_bytes(uint32_t bytes) const
    {
        if (static_cast<std::size_t>(m_end - m_it) < bytes) {
            throw std::runtime_error{"Invalid WKB geometry: Incomplete"};
        }
    }

    uint32_t parse_uint32()
    {
        check_bytes(sizeof(uint32_t));

        uint32_t data = 0;
        std::memcpy(&data, m_it, sizeof(uint32_t));
        m_it += sizeof(uint32_t);

        return data;
    }

    /**
     * Get length field and check it against an upper bound based on the
     * maximum number of points which could theoretically be stored in a string
     * of the size of the input string (each point takes up at least
     * 2*sizeof(double) bytes. This prevents an invalid WKB from leading us to
     * reserve huge amounts of memory without having to define a constant upper
     * bound.
     */
    uint32_t parse_length()
    {
        auto const length = parse_uint32();
        if (length > m_max_length) {
            throw std::runtime_error{"Invalid WKB geometry: Length too large"};
        }
        return length;
    }

    uint32_t parse_header(geom::geometry_t *geom = nullptr)
    {
        if (static_cast<uint8_t>(*m_it++) !=
            ewkb::wkb_byte_order_type_t::Endian) {
            throw std::runtime_error
            {
#if __BYTE_ORDER == __LITTLE_ENDIAN
                "Geometries in the database are returned in big-endian byte "
                "order. "
#else
                "Geometries in the database are returned in little-endian byte "
                "order. "
#endif
                "osm2pgsql can only process geometries in native byte order."
            };
        }

        auto type = parse_uint32();
        if (type & ewkb::geometry_type::wkb_srid) {
            if (!geom) {
                // If geom is not set than this is one of the geometries
                // in a GeometryCollection or a Multi... geometry. They
                // should not have a SRID set, because the SRID is already
                // on the outer geometry.
                throw std::runtime_error{
                    "Invalid WKB geometry: SRID set in geometry of collection"};
            }
            type &= ~ewkb::geometry_type::wkb_srid;
            geom->set_srid(static_cast<int>(parse_uint32()));
        }
        return type;
    }

    void parse_point(geom::point_t *point)
    {
        check_bytes(sizeof(double) * 2);

        std::array<double, 2> data{};
        std::memcpy(&data[0], m_it, sizeof(double) * 2);
        m_it += sizeof(double) * 2;

        point->set_x(data[0]);
        point->set_y(data[1]);
    }

    void parse_point_list(geom::point_list_t *points, uint32_t min_points)
    {
        auto const num_points = parse_length();
        if (num_points < min_points) {
            throw std::runtime_error{
                "Invalid WKB geometry: {} are not"
                " enough points (must be at least {})"_format(num_points,
                                                              min_points)};
        }
        points->reserve(num_points);
        for (uint32_t i = 0; i < num_points; ++i) {
            parse_point(&points->emplace_back());
        }
    }

    void parse_polygon(geom::polygon_t *polygon)
    {
        auto const num_rings = parse_length();
        if (num_rings == 0) {
            throw std::runtime_error{
                "Invalid WKB geometry: Polygon without rings"};
        }
        parse_point_list(&polygon->outer(), 4);

        polygon->inners().reserve(num_rings - 1);
        for (uint32_t i = 1; i < num_rings; ++i) {
            parse_point_list(&polygon->inners().emplace_back(), 4);
        }
    }

    void parse_multi_linestring(geom::geometry_t *geom)
    {
        auto &multilinestring = geom->set<geom::multilinestring_t>();
        auto const num_geoms = parse_length();
        if (num_geoms == 0) {
            throw std::runtime_error{
                "Invalid WKB geometry: Multilinestring without linestrings"};
        }

        multilinestring.reserve(num_geoms);
        for (uint32_t i = 0; i < num_geoms; ++i) {
            auto &linestring = multilinestring.emplace_back();
            uint32_t const type = parse_header();
            if (type != geometry_type::wkb_line) {
                throw std::runtime_error{
                    "Invalid WKB geometry: Multilinestring containing"
                    " something other than linestring: {}"_format(type)};
            }
            parse_point_list(&linestring, 2);
        }
    }

    void parse_multi_polygon(geom::geometry_t *geom)
    {
        auto &multipolygon = geom->set<geom::multipolygon_t>();
        auto const num_geoms = parse_length();
        if (num_geoms == 0) {
            throw std::runtime_error{
                "Invalid WKB geometry: Multipolygon without polygons"};
        }

        multipolygon.reserve(num_geoms);
        for (uint32_t i = 0; i < num_geoms; ++i) {
            auto &polygon = multipolygon.emplace_back();
            uint32_t const type = parse_header();
            if (type != geometry_type::wkb_polygon) {
                throw std::runtime_error{
                    "Invalid WKB geometry: Multipolygon containing"
                    " something other than polygon: {}"_format(type)};
            }
            parse_polygon(&polygon);
        }
    }

    char const *m_it;
    char const *m_end;
    uint32_t m_max_length;

}; // class ewkb_parser_t

} // anonymous namespace

} // namespace ewkb

std::string geom_to_ewkb(geom::geometry_t const &geom, bool ensure_multi)
{
    return geom.visit(ewkb::make_ewkb_visitor{
        static_cast<uint32_t>(geom.srid()), ensure_multi});
}

geom::geometry_t ewkb_to_geom(std::string const &wkb)
{
    ewkb::ewkb_parser_t parser{wkb};
    return parser();
}

unsigned char decode_hex_char(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    throw std::runtime_error{"Invalid wkb: Not a hex character"};
}

std::string decode_hex(char const *hex)
{
    std::string wkb;

    while (*hex != '\0') {
        unsigned int const c = decode_hex_char(*hex++);

        if (*hex == '\0') {
            throw std::runtime_error{"Invalid wkb: Not a valid hex string"};
        }

        wkb += static_cast<char>((c << 4U) | decode_hex_char(*hex++));
    }

    return wkb;
}
