#ifndef OSM2PGSQL_GEN_GROUPED_LINEMERGE_HPP
#define OSM2PGSQL_GEN_GROUPED_LINEMERGE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-base.hpp"

#include <cstddef>
#include <string_view>

class params_t;
class pg_conn_t;

/**
 * Generalization strategy "grouped-linemerge".
 *
 * Globally merges connected LineStrings that share the same set of grouping
 * column values, equivalent to
 *
 *   INSERT INTO dest (cols..., geom)
 *     SELECT cols..., (ST_Dump(ST_LineMerge(ST_Collect(geom)))).geom
 *       FROM src GROUP BY cols...;
 *
 * Unlike the tile-based strategies this does NOT clip to tiles and the
 * destination geometries are global merged lines, not tile-keyed rows.
 *
 * In append (update) mode the work is done incrementally and locally: the
 * expire table (populated by osm2pgsql during the update) is used only as a
 * seed for "where did line geometry change". For every changed region we
 * walk the connected component(s) of matching lines out from the seed (via a
 * recursive query), delete the merged outputs that overlap the region and
 * regenerate them from scratch. This keeps each update bounded to the local
 * connected component instead of re-merging the whole planet.
 */
class gen_grouped_linemerge_t : public gen_base_t
{
public:
    gen_grouped_linemerge_t(pg_conn_t *connection, bool append,
                            params_t *params);

    void process() override;

    std::string_view strategy() const noexcept override
    {
        return "grouped-linemerge";
    }

private:
    /// Build the whole dest table from scratch (create mode).
    void process_create();

    /// Incrementally update the dest table from the expire list (append mode).
    void process_append();

    std::size_t m_timer_merge;
    std::size_t m_timer_walk;
    std::size_t m_timer_delete;
};

#endif // OSM2PGSQL_GEN_GROUPED_LINEMERGE_HPP
