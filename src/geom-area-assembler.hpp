#ifndef OSM2PGSQL_GEOM_AREA_ASSEMBLER_HPP
#define OSM2PGSQL_GEOM_AREA_ASSEMBLER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Code for assembling areas from ways or relations.
 */

#include <osmium/area/detail/basic_assembler.hpp>

namespace geom {

/**
 * The area_assembler_t class is a light wrapper around the libosmium class
 * for assembling areas.
 */
class area_assembler_t : public osmium::area::detail::BasicAssembler
{
public:
    explicit area_assembler_t(osmium::memory::Buffer *buffer);

    /**
     * Assemble an area from the given way.
     *
     * @returns false if there was some kind of error building the
     *          area, true otherwise.
     */
    bool operator()(osmium::Way const &way);

    /**
     * Assemble an area from the given relation and its member ways
     * which are in the ways_buffer.
     *
     * @returns false if there was some kind of error building the
     *          area, true otherwise.
     */
    bool operator()(osmium::Relation const &relation,
                    osmium::memory::Buffer const &ways_buffer);

    /**
     * Access the area that was built last.
     */
    osmium::Area const &get_area() const noexcept
    {
        return m_buffer->get<osmium::Area>(0);
    }

private:
    bool make_area();

    osmium::memory::Buffer *m_buffer;

}; // class area_assembler_t

} // namespace geom

#endif // OSM2PGSQL_GEOM_AREA_ASSEMBLER_HPP
