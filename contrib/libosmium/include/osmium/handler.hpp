#ifndef OSMIUM_HANDLER_HPP
#define OSMIUM_HANDLER_HPP

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

namespace osmium {

    class Area;
    class Changeset;
    class ChangesetDiscussion;
    class InnerRing;
    class Node;
    class OSMObject;
    class OuterRing;
    class Relation;
    class RelationMemberList;
    class TagList;
    class Way;
    class WayNodeList;

    /**
     * @brief Osmium handlers provide callbacks for OSM objects
     */
    namespace handler {

        /**
         * Handler base class. Never used directly. Derive your own class from
         * this class and "overwrite" the functions. Your functions must be
         * named the same, but don't have to be const or noexcept or take
         * their argument as const.
         *
         * Usually you will overwrite the node(), way(), and relation()
         * functions. If your program supports multipolygons, also the area()
         * function. You can also use the osm_object() function which is
         * called for all OSM objects (nodes, ways, relations, and areas)
         * right before each of their specific callbacks is called.
         *
         * If you are working with changesets, implement the changeset()
         * function.
         */
        class Handler {

        public:

            void osm_object(const osmium::OSMObject& /*osm_object*/) const noexcept {
            }

            void node(const osmium::Node& /*node*/) const noexcept {
            }

            void way(const osmium::Way& /*way*/) const noexcept {
            }

            void relation(const osmium::Relation& /*relation*/) const noexcept {
            }

            void area(const osmium::Area& /*area*/) const noexcept {
            }

            void changeset(const osmium::Changeset& /*changeset*/) const noexcept {
            }

            void tag_list(const osmium::TagList& /*tag_list*/) const noexcept {
            }

            void way_node_list(const osmium::WayNodeList& /*way_node_list*/) const noexcept {
            }

            void relation_member_list(const osmium::RelationMemberList& /*relation_member_list*/) const noexcept {
            }

            void outer_ring(const osmium::OuterRing& /*outer_ring*/) const noexcept {
            }

            void inner_ring(const osmium::InnerRing& /*inner_ring*/) const noexcept {
            }

            void changeset_discussion(const osmium::ChangesetDiscussion& /*changeset_discussion*/) const noexcept {
            }

            void flush() const noexcept {
            }

        }; // class Handler

    } // namespace handler

} // namespace osmium

#endif // OSMIUM_HANDLER_HPP
