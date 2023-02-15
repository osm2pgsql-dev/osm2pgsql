/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-tiles.hpp"
#include "flex-tileset.hpp"

std::size_t flex_tileset_t::output(quadkey_list_t const &tile_list,
                                   std::string const &conninfo) const
{
    std::size_t num = 0;
    if (!m_filename.empty()) {
        num = output_tiles_to_file(tile_list, m_minzoom, m_maxzoom,
                                   m_filename.c_str());
    }
    if (!m_table.empty()) {
        num = output_tiles_to_table(tile_list, m_minzoom, m_maxzoom, conninfo,
                                    m_schema, m_table);
    }
    return num;
}
