#ifndef OSMIUM_GEOM_GEOJSON_HPP
#define OSMIUM_GEOM_GEOJSON_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2023 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/geom/coordinates.hpp>
#include <osmium/geom/factory.hpp>

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

namespace osmium {

    namespace geom {

        namespace detail {

            class GeoJSONFactoryImpl {

                std::string m_str;
                int m_precision;

            public:

                using point_type        = std::string;
                using linestring_type   = std::string;
                using polygon_type      = std::string;
                using multipolygon_type = std::string;
                using ring_type         = std::string;

                explicit GeoJSONFactoryImpl(int /*srid*/, int precision = 7) :
                    m_precision(precision) {
                }

                /* Point */

                // { "type": "Point", "coordinates": [100.0, 0.0] }
                point_type make_point(const osmium::geom::Coordinates& xy) const {
                    std::string str{"{\"type\":\"Point\",\"coordinates\":"};
                    xy.append_to_string(str, '[', ',', ']', m_precision);
                    str += "}";
                    return str;
                }

                /* LineString */

                // { "type": "LineString", "coordinates": [ [100.0, 0.0], [101.0, 1.0] ] }
                void linestring_start() {
                    m_str = "{\"type\":\"LineString\",\"coordinates\":[";
                }

                void linestring_add_location(const osmium::geom::Coordinates& xy) {
                    xy.append_to_string(m_str, '[', ',', ']', m_precision);
                    m_str += ',';
                }

                linestring_type linestring_finish(size_t /*num_points*/) {
                    assert(!m_str.empty());
                    std::string str;

                    using std::swap;
                    swap(str, m_str);

                    str.back() = ']';
                    str += "}";
                    return str;
                }

                /* Polygon */
                void polygon_start() {
                    m_str = "{\"type\":\"Polygon\",\"coordinates\":[[";
                }

                void polygon_add_location(const osmium::geom::Coordinates& xy) {
                    xy.append_to_string(m_str, '[', ',', ']', m_precision);
                    m_str += ',';
                }

                polygon_type polygon_finish(size_t /*num_points*/) {
                    assert(!m_str.empty());
                    std::string str;

                    using std::swap;
                    swap(str, m_str);

                    str.back() = ']';
                    str += "]}";
                    return str;
                }

                /* MultiPolygon */

                void multipolygon_start() {
                    m_str = "{\"type\":\"MultiPolygon\",\"coordinates\":[";
                }

                void multipolygon_polygon_start() {
                    m_str += '[';
                }

                void multipolygon_polygon_finish() {
                    m_str += "],";
                }

                void multipolygon_outer_ring_start() {
                    m_str += '[';
                }

                void multipolygon_outer_ring_finish() {
                    assert(!m_str.empty());
                    m_str.back() = ']';
                }

                void multipolygon_inner_ring_start() {
                    m_str += ",[";
                }

                void multipolygon_inner_ring_finish() {
                    assert(!m_str.empty());
                    m_str.back() = ']';
                }

                void multipolygon_add_location(const osmium::geom::Coordinates& xy) {
                    xy.append_to_string(m_str, '[', ',', ']', m_precision);
                    m_str += ',';
                }

                multipolygon_type multipolygon_finish() {
                    assert(!m_str.empty());
                    std::string str;

                    using std::swap;
                    swap(str, m_str);

                    str.back() = ']';
                    str += "}";
                    return str;
                }

            }; // class GeoJSONFactoryImpl

        } // namespace detail

        template <typename TProjection = IdentityProjection>
        using GeoJSONFactory = GeometryFactory<osmium::geom::detail::GeoJSONFactoryImpl, TProjection>;

    } // namespace geom

} // namespace osmium

#endif // OSMIUM_GEOM_GEOJSON_HPP
