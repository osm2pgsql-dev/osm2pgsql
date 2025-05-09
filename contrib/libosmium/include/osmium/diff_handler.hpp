#ifndef OSMIUM_DIFF_HANDLER_HPP
#define OSMIUM_DIFF_HANDLER_HPP

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

#include <osmium/osm/diff_object.hpp>

namespace osmium {

    /**
     * @brief Osmium diff handlers provide access to differences between OSM object versions
     */
    namespace diff_handler {

        class DiffHandler {

        public:

            DiffHandler() = default;

            void node(const osmium::DiffNode& /*node*/) const noexcept {
            }

            void way(const osmium::DiffWay& /*way*/) const noexcept {
            }

            void relation(const osmium::DiffRelation& /*relation*/) const noexcept {
            }

        }; // class DiffHandler

    } // namespace diff_handler

} // namespace osmium

#endif // OSMIUM_DIFF_HANDLER_HPP
