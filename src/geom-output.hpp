#ifndef OSM2PGSQL_GEOM_OUTPUT_HPP
#define OSM2PGSQL_GEOM_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-functions.hpp"
#include "geom.hpp"

/**
 * \file
 *
 * Functions to output geometries to an output stream in something similar to
 * the WKT format.
 *
 * This is used for debugging: The Catch framework can use these to output
 * geometries when a test fails.
 */

namespace geom {

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const nullgeom_t & /*input*/)
{
    return out << "NULL";
}

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const point_t &input)
{
    return out << input.x() << ' ' << input.y();
}

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const point_list_t &input)
{
    if (input.empty()) {
        return out << "EMPTY";
    }
    auto it = input.cbegin();
    out << *it;
    for (++it; it != input.cend(); ++it) {
        out << ',' << *it;
    }
    return out;
}

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const polygon_t &input)
{
    out << '(' << input.outer() << ')';
    for (auto const &ring : input.inners()) {
        out << ",(" << ring << ')';
    }
    return out;
}

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const collection_t &input)
{
    if (input.num_geometries() == 0) {
        return out << "EMPTY";
    }
    auto it = input.cbegin();
    out << *it;
    for (++it; it != input.cend(); ++it) {
        out << ',' << *it;
    }
    return out;
}

template <typename CHAR, typename TRAITS, typename GEOM>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out,
           const multigeometry_t<GEOM> &input)
{
    if (input.num_geometries() == 0) {
        return out << "EMPTY";
    }
    auto it = input.cbegin();
    out << '(' << *it << ')';
    for (++it; it != input.cend(); ++it) {
        out << ",(" << *it << ')';
    }
    return out;
}

template <typename CHAR, typename TRAITS>
std::basic_ostream<CHAR, TRAITS> &
operator<<(std::basic_ostream<CHAR, TRAITS> &out, const geometry_t &geom)
{
    out << geometry_type(geom) << '(';
    geom.visit(overloaded{[&](auto const &input) { out << input; }});
    return out << ')';
}

} // namespace geom

#endif // OSM2PGSQL_GEOM_OUTPUT_HPP
