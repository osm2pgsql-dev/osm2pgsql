#ifndef OSMIUM_AREA_DETAIL_NODE_REF_SEGMENT_HPP
#define OSMIUM_AREA_DETAIL_NODE_REF_SEGMENT_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2025 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <osmium/area/detail/vector.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/node_ref.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <utility>

namespace osmium {

    class Way;

    namespace area {

        /**
         * @brief Namespace for Osmium internal use
         */
        namespace detail {

            class ProtoRing;

            enum class role_type : uint8_t {
                unknown = 0,
                outer   = 1,
                inner   = 2,
                empty   = 3
            };

            /**
             * This helper class for the Assembler class models a segment,
             * the connection between two nodes.
             *
             * Internally segments have their smaller coordinate at the
             * beginning of the segment. Smaller, in this case, means smaller
             * x coordinate, and, if they are the same, smaller y coordinate.
             */
            class NodeRefSegment {

                // First node in order described above.
                osmium::NodeRef m_first;

                // Second node in order described above.
                osmium::NodeRef m_second;

                // Way this segment was from.
                const osmium::Way* m_way = nullptr;

                // The ring this segment is part of. Initially nullptr, this
                // will be filled in once we know which ring the segment is in.
                ProtoRing* m_ring = nullptr;

                // The role of this segment from the member role.
                role_type m_role = role_type::unknown;

                // Nodes have to be reversed to get the intended order.
                bool m_reverse = false;

                // We found the right direction for this segment in the ring.
                // (This depends on whether it is an inner or outer ring.)
                bool m_direction_done = false;

            public:

                NodeRefSegment() noexcept = default;

                NodeRefSegment(const osmium::NodeRef& nr1, const osmium::NodeRef& nr2, role_type role, const osmium::Way* way) noexcept :
                    m_first(nr1.location() < nr2.location() ? nr1 : nr2),
                    m_second(nr1.location() < nr2.location() ? nr2 : nr1),
                    m_way(way),
                    m_role(role) {
                }

                /**
                 * The ring this segment is a part of. nullptr if we don't
                 * have the ring yet.
                 */
                ProtoRing* ring() const noexcept {
                    return m_ring;
                }

                /**
                 * Returns true if the segment has already been placed in a
                 * ring.
                 */
                bool is_done() const noexcept {
                    return m_ring != nullptr;
                }

                void set_ring(ProtoRing* ring) noexcept {
                    assert(ring);
                    m_ring = ring;
                }

                bool is_reverse() const noexcept {
                    return m_reverse;
                }

                void reverse() noexcept {
                    m_reverse = !m_reverse;
                }

                bool is_direction_done() const noexcept {
                    return m_direction_done;
                }

                void mark_direction_done() noexcept {
                    m_direction_done = true;
                }

                void mark_direction_not_done() noexcept {
                    m_direction_done = false;
                }

                /**
                 * Return first NodeRef of Segment according to sorting
                 * order (bottom left to top right).
                 */
                const osmium::NodeRef& first() const noexcept {
                    return m_first;
                }

                /**
                 * Return second NodeRef of Segment according to sorting
                 * order (bottom left to top right).
                 */
                const osmium::NodeRef& second() const noexcept {
                    return m_second;
                }

                /**
                 * Return real first NodeRef of Segment.
                 */
                const osmium::NodeRef& start() const noexcept {
                    return m_reverse ? m_second : m_first;
                }

                /**
                 * Return real second NodeRef of Segment.
                 */
                const osmium::NodeRef& stop() const noexcept {
                    return m_reverse ? m_first : m_second;
                }

                bool role_outer() const noexcept {
                    return m_role == role_type::outer;
                }

                bool role_inner() const noexcept {
                    return m_role == role_type::inner;
                }

                bool role_empty() const noexcept {
                    return m_role == role_type::empty;
                }

                const char* role_name() const noexcept {
                    static const std::array<const char*, 4> names = {{"unknown", "outer", "inner", "empty"}};
                    return names[static_cast<int>(m_role)];
                }

                const osmium::Way* way() const noexcept {
                    return m_way;
                }

                /**
                 * The "determinant" of this segment. Used for calculating
                 * the winding order of a ring.
                 */
                int64_t det() const noexcept {
                    const vec a{start()};
                    const vec b{stop()};
                    return a * b;
                }

            }; // class NodeRefSegment

            /// NodeRefSegments are equal if both their locations are equal
            inline bool operator==(const NodeRefSegment& lhs, const NodeRefSegment& rhs) noexcept {
                return lhs.first().location() == rhs.first().location() &&
                       lhs.second().location() == rhs.second().location();
            }

            inline bool operator!=(const NodeRefSegment& lhs, const NodeRefSegment& rhs) noexcept {
                return !(lhs == rhs);
            }

            /**
             * A NodeRefSegment is "smaller" if the first point is to the
             * left and down of the first point of the second segment.
             * If both first points are the same, the segment with the higher
             * slope comes first. If the slope is the same, the shorter
             * segment comes first.
             */
            inline bool operator<(const NodeRefSegment& lhs, const NodeRefSegment& rhs) noexcept {
                if (lhs.first().location() == rhs.first().location()) {
                    const vec p0{lhs.first().location()};
                    const vec p1{lhs.second().location()};
                    const vec q0{rhs.first().location()};
                    const vec q1{rhs.second().location()};
                    const vec p = p1 - p0;
                    const vec q = q1 - q0;

                    if (p.x == 0 && q.x == 0) {
                        return p.y < q.y;
                    }

                    const auto a = p.y * q.x;
                    const auto b = q.y * p.x;
                    if (a == b) {
                        return p.x < q.x;
                    }
                    return a > b;
                }
                return lhs.first().location() < rhs.first().location();
            }

            template <typename TChar, typename TTraits>
            inline std::basic_ostream<TChar, TTraits>& operator<<(std::basic_ostream<TChar, TTraits>& out, const NodeRefSegment& segment) {
                return out << segment.start() << "--" << segment.stop()
                           << "[" << (segment.is_reverse() ? 'R' : '_')
                                  << (segment.is_done()    ? 'd' : '_')
                                  << (segment.is_direction_done() ? 'D' : '_') << "]";
            }

            inline bool outside_x_range(const NodeRefSegment& s1, const NodeRefSegment& s2) noexcept {
                return s1.first().location().x() > s2.second().location().x();
            }

            inline bool y_range_overlap(const NodeRefSegment& s1, const NodeRefSegment& s2) noexcept {
                const std::pair<int32_t, int32_t> m1 = std::minmax(s1.first().location().y(), s1.second().location().y());
                const std::pair<int32_t, int32_t> m2 = std::minmax(s2.first().location().y(), s2.second().location().y());
                return !(m1.first > m2.second || m2.first > m1.second);
            }

            /**
             * Calculate the intersection between two NodeRefSegments. The
             * result is returned as a Location. Note that because the Location
             * uses integers with limited precision internally, the result
             * might be slightly different than the numerically correct
             * location.
             *
             * This function uses integer arithmetic as much as possible and
             * will not work if the segments are longer than about half the
             * planet. This shouldn't happen with real data, so it isn't a big
             * problem.
             *
             * If the segments touch in one or both of their endpoints, it
             * doesn't count as an intersection.
             *
             * If the segments intersect not in a single point but in multiple
             * points, ie if they are collinear and overlap, the smallest
             * of the endpoints that is in the overlapping section is returned.
             *
             * @returns Undefined osmium::Location if there is no intersection
             *          or a defined Location if the segments intersect.
             */
            inline osmium::Location calculate_intersection(const NodeRefSegment& s1, const NodeRefSegment& s2) noexcept {
                // See https://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect
                // for some hints about how the algorithm works.
                const vec p0{s1.first()};
                const vec p1{s1.second()};
                const vec q0{s2.first()};
                const vec q1{s2.second()};

                if ((p0 == q0 && p1 == q1) ||
                    (p0 == q1 && p1 == q0)) {
                    // segments are the same
                    return osmium::Location{};
                }

                const vec pd = p1 - p0;
                const int64_t d = pd * (q1 - q0);

                if (d != 0) {
                    // segments are not collinear

                    if (p0 == q0 || p0 == q1 || p1 == q0 || p1 == q1) {
                        // touching at an end point
                        return osmium::Location{};
                    }

                    // intersection in a point

                    const int64_t na = ((q1.x - q0.x) * (p0.y - q0.y)) -
                                       ((q1.y - q0.y) * (p0.x - q0.x));

                    const int64_t nb = ((p1.x - p0.x) * (p0.y - q0.y)) -
                                       ((p1.y - p0.y) * (p0.x - q0.x));

                    if ((d > 0 && na >= 0 && na <= d && nb >= 0 && nb <= d) ||
                        (d < 0 && na <= 0 && na >= d && nb <= 0 && nb >= d)) {
                        const double ua = static_cast<double>(na) / static_cast<double>(d);
                        const vec i = p0 + ua * (p1 - p0);
                        return osmium::Location{static_cast<int32_t>(i.x), static_cast<int32_t>(i.y)};
                    }

                    return osmium::Location{};
                }

                // segments are collinear

                if (pd * (q0 - p0) == 0) {
                    // segments are on the same line

                    struct seg_loc {
                        int segment;
                        osmium::Location location;
                    };

                    std::array<seg_loc, 4> sl = {{
                        {0, s1.first().location()},
                        {0, s1.second().location()},
                        {1, s2.first().location()},
                        {1, s2.second().location()},
                    }};

                    std::sort(sl.begin(), sl.end(), [](const seg_loc& lhs, const seg_loc& rhs) {
                        return lhs.location < rhs.location;
                    });

                    if (sl[1].location == sl[2].location) {
                        return osmium::Location();
                    }

                    if (sl[0].segment != sl[1].segment) {
                        if (sl[0].location == sl[1].location) {
                            return sl[2].location;
                        }
                        return sl[1].location;
                    }
                }

                return osmium::Location{};
            }

        } // namespace detail

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_DETAIL_NODE_REF_SEGMENT_HPP
