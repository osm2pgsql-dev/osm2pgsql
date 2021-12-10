#ifndef OSM2PGSQL_WKB_HPP
#define OSM2PGSQL_WKB_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include <osmium/geom/coordinates.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

/**
 * \file
 *
 * Functions for converting geometries from and to (E)WKB.
 */

namespace ewkb {

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

/**
 * Class that allows to iterate over the elements of a ewkb geometry.
 *
 * Note: this class assumes that the wkb was created by ewkb::writer_t.
 *       It implements the exact opposite decoding.
 */
class parser_t
{
public:
    inline static std::string wkb_from_hex(std::string const &wkb)
    {
        std::string out;

        bool front = true;
        char outc;
        for (char c : wkb) {
            c -= 48;
            if (c > 9) {
                c -= 7;
            }
            if (front) {
                outc = char(c << 4);
                front = false;
            } else {
                out += outc | c;
                front = true;
            }
        }

        if (out[0] != Endian) {
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
        return out;
    }

    explicit parser_t(std::string const &wkb) noexcept : m_wkb(&wkb) {}

    std::size_t save_pos() const noexcept { return m_pos; }

    void rewind(std::size_t pos) noexcept { m_pos = pos; }

    uint32_t read_header()
    {
        m_pos += sizeof(uint8_t); // skip endianess marker

        auto const type = read_data<uint32_t>();

        if (type & wkb_srid) {
            m_pos += sizeof(uint32_t); // skip SRID
        }

        return type & 0xffU;
    }

    uint32_t read_length() { return read_data<uint32_t>(); }

    osmium::geom::Coordinates read_point()
    {
        auto const x = read_data<double>();
        auto const y = read_data<double>();

        return osmium::geom::Coordinates{x, y};
    }

    void skip_points(std::size_t num)
    {
        auto const length = sizeof(double) * 2 * num;
        check_available(length);
        m_pos += length;
    }

private:
    void check_available(std::size_t length)
    {
        if (m_pos + length > m_wkb->size()) {
            throw std::runtime_error{"Invalid EWKB geometry found"};
        }
    }

    template <typename T>
    T read_data()
    {
        check_available(sizeof(T));

        T data;
        std::memcpy(&data, m_wkb->data() + m_pos, sizeof(T));
        m_pos += sizeof(T);

        return data;
    }

    std::string const *m_wkb;
    std::size_t m_pos = 0;
};

} // namespace ewkb

/**
 * Convert single geometry to EWKB
 *
 * \param geom Input geometry
 * \param ensure_multi Wrap non-multi geometries in multi geometries
 * \returns String with EWKB encoded geometry
 */
std::string geom_to_ewkb(geom::geometry_t const &geom,
                         bool ensure_multi = false);

#endif // OSM2PGSQL_WKB_HPP
