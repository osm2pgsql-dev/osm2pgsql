/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-area-assembler.hpp"

#include <osmium/builder/osm_object_builder.hpp>

namespace geom {

osmium::area::AssemblerConfig const area_config;

area_assembler_t::area_assembler_t(osmium::memory::Buffer *buffer)
: osmium::area::detail::BasicAssembler(area_config), m_buffer(buffer)
{
}

bool area_assembler_t::make_area()
{
    if (!create_rings()) {
        return false;
    }

    m_buffer->clear();
    {
        osmium::builder::AreaBuilder builder{*m_buffer};
        add_rings_to_area(builder);
    }
    m_buffer->commit();

    return true;
}

bool area_assembler_t::operator()(osmium::Way const &way)
{
    segment_list().extract_segments_from_way(nullptr, stats().duplicate_nodes,
                                             way);
    return make_area();
}

// Currently the relation is not needed for assembling the area, because
// the roles on the members are ignored. In the future we might want to use
// the roles, so we leave the function signature as it is.
bool area_assembler_t::operator()(osmium::Relation const & /*relation*/,
                                  osmium::memory::Buffer const &ways_buffer)
{
    for (auto const &way : ways_buffer.select<osmium::Way>()) {
        segment_list().extract_segments_from_way(nullptr,
                                                 stats().duplicate_nodes, way);
    }
    return make_area();
}

} // namespace geom
