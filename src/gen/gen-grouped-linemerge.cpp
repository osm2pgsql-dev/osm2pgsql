/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-grouped-linemerge.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"

#include <osmium/util/string.hpp>

#include <cstdlib>
#include <string>

namespace {

std::string trim(std::string const &str)
{
    auto const begin = str.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return {};
    }
    auto const end = str.find_last_not_of(" \t\n\r");
    return str.substr(begin, end - begin + 1);
}

} // anonymous namespace

gen_grouped_linemerge_t::gen_grouped_linemerge_t(pg_conn_t *connection,
                                                 bool append, params_t *params)
: gen_base_t(connection, append, params), m_timer_merge(add_timer("merge")),
  m_timer_walk(add_timer("walk")), m_timer_delete(add_timer("delete"))
{
    check_src_dest_table_params_exist();

    // Parse the comma-separated list of grouping columns into the SQL
    // fragments we need:
    //  * group_cols      - quoted list "a", "b"        (SELECT/GROUP BY/INSERT)
    //  * group_cols_l    - l."a", l."b"                (read from source alias)
    //  * group_cols_l_gk - l."a" AS "gk_a", ...        (frontier of the walk)
    //  * group_cols_gk   - "a" AS "gk_a", ...          (seed of the walk)
    //  * group_join      - l."a" IS NOT DISTINCT FROM n."gk_a" AND ...
    // The walk's frontier table carries the grouping values under "gk_"-
    // prefixed names so that an (optional) user 'where' filter, which uses the
    // unqualified source column names, is never ambiguous against the frontier.
    // IS NOT DISTINCT FROM makes NULLs (e.g. an unnamed road) compare equal,
    // matching the GROUP BY semantics.
    auto const group_by = get_params().get_string("group_by_columns", "");
    if (group_by.empty()) {
        throw fmt_error("Missing 'group_by_columns' parameter in"
                        " generalizer{}.",
                        context());
    }

    std::string group_cols;
    std::string group_cols_l;
    std::string group_cols_l_gk;
    std::string group_cols_gk;
    std::string group_join;    // l (source) vs n (reached node, gk_ prefixed)
    std::string group_join_dn; // d (dest) vs n (reached node, gk_ prefixed)
    bool first = true;
    for (auto const &raw : osmium::split_string(group_by, ',')) {
        auto const col = trim(raw);
        if (col.empty()) {
            continue;
        }
        check_identifier(col, "group_by_columns");
        if (!first) {
            group_cols += ", ";
            group_cols_l += ", ";
            group_cols_l_gk += ", ";
            group_cols_gk += ", ";
            group_join += " AND ";
            group_join_dn += " AND ";
        }
        group_cols += fmt::format(R"("{}")", col);
        group_cols_l += fmt::format(R"(l."{}")", col);
        group_cols_l_gk += fmt::format(R"(l."{0}" AS "gk_{0}")", col);
        group_cols_gk += fmt::format(R"("{0}" AS "gk_{0}")", col);
        group_join +=
            fmt::format(R"(l."{0}" IS NOT DISTINCT FROM n."gk_{0}")", col);
        group_join_dn +=
            fmt::format(R"(d."{0}" IS NOT DISTINCT FROM n."gk_{0}")", col);
        first = false;
    }
    if (first) {
        throw fmt_error("Parameter 'group_by_columns' is empty in"
                        " generalizer{}.",
                        context());
    }

    params->set("group_cols", group_cols);
    params->set("group_cols_l", group_cols_l);
    params->set("group_cols_l_gk", group_cols_l_gk);
    params->set("group_cols_gk", group_cols_gk);
    params->set("group_join", group_join);
    params->set("group_join_dn", group_join_dn);

    // Optional pre-filter. Lines not matching this are completely excluded
    // from the generalization (they never enter the destination table and are
    // never walked). The filter is a SQL boolean expression on the source
    // columns. We default to 'true' so it composes everywhere, and build a
    // matching predicate for the (partial) endpoint indexes.
    auto const filter = get_params().get_string("where", "");
    params->set("where", filter.empty() ? std::string{"true"}
                                        : "(" + filter + ")");
    params->set("index_predicate",
                filter.empty() ? std::string{} : "WHERE (" + filter + ")");

    // Names for the functional endpoint indexes optionally created on the
    // source table in create mode (and needed for fast walks in append mode).
    auto const src_table = get_params().get_identifier("src_table");
    params->set("idx_startpt", src_table + "_glm_startpt");
    params->set("idx_endpt", src_table + "_glm_endpt");

    // The append-mode delete looks up destination rows by their endpoint
    // points, so the destination gets matching functional indexes too.
    auto const dest_table = get_params().get_identifier("dest_table");
    params->set("idx_dest_startpt", dest_table + "_glm_startpt");
    params->set("idx_dest_endpt", dest_table + "_glm_endpt");

    if (append_mode()) {
        if (!get_params().has("endpoint_table")) {
            throw fmt_error("Missing 'endpoint_table' parameter in"
                            " generalizer{} (required in append mode).",
                            context());
        }
        params->set("endpoints", qualified_name(get_params().get_identifier(
                                                     "schema"),
                                                 get_params().get_identifier(
                                                     "endpoint_table")));
    }
}

void gen_grouped_linemerge_t::process()
{
    if (append_mode()) {
        process_append();
    } else {
        process_create();
    }
}

void gen_grouped_linemerge_t::process_create()
{
    if (get_params().get_bool("create_indexes", true)) {
        log_gen("Creating endpoint indexes on source table...");
        dbexec(
            R"(CREATE INDEX IF NOT EXISTS "{idx_startpt}" ON {src} USING btree)"
            R"( (ST_StartPoint("{geom_column}")) {index_predicate})");
        dbexec(
            R"(CREATE INDEX IF NOT EXISTS "{idx_endpt}" ON {src} USING btree)"
            R"( (ST_EndPoint("{geom_column}")) {index_predicate})");
    }

    log_gen("Merging lines by group...");
    timer(m_timer_merge).start();
    connection().exec("BEGIN");
    dbexec("TRUNCATE {dest}");
    auto const result = dbexec(R"(
INSERT INTO {dest} ({group_cols}, "{geom_column}")
 SELECT {group_cols},
        (ST_Dump(ST_LineMerge(ST_Collect("{geom_column}"
            ORDER BY "{geom_column}")))).geom
   FROM {src}
  WHERE {where}
  GROUP BY {group_cols}
)");
    connection().exec("COMMIT");
    timer(m_timer_merge).stop();
    log_gen("Inserted {} merged linestrings.", result.affected_rows());

    dbexec("ANALYZE {dest}");

    if (get_params().get_bool("create_indexes", true)) {
        log_gen("Creating endpoint indexes on destination table...");
        dbexec(R"(CREATE INDEX IF NOT EXISTS "{idx_dest_startpt}" ON {dest})"
               R"( USING btree (ST_StartPoint("{geom_column}")))");
        dbexec(R"(CREATE INDEX IF NOT EXISTS "{idx_dest_endpt}" ON {dest})"
               R"( USING btree (ST_EndPoint("{geom_column}")))");
    }
}

void gen_grouped_linemerge_t::process_append()
{
    connection().exec("BEGIN");

    // Step 1: Consume the exact endpoints of every way added/edited/deleted in
    // this update. They were captured by the expire output's endpoint table
    // (deletes contribute the old way's endpoints via the geometry cache). We
    // seed and clean up by exact endpoint equality, so the functional endpoint
    // btree indexes are used and unrelated roads merely passing nearby are not
    // touched.
    dbexec(R"(
CREATE TEMP TABLE _glm_points ON COMMIT DROP AS
 WITH consumed AS (
   DELETE FROM {endpoints} RETURNING "geom"
 )
 SELECT DISTINCT "geom" AS pt FROM consumed
)");

    auto const point_count = dbexec("SELECT count(*) FROM _glm_points");
    if (std::strtoll(point_count.get_value(0, 0), nullptr, 10) == 0) {
        log_gen("No changed endpoints, nothing to do.");
        connection().exec("COMMIT");
        return;
    }

    // No spatial index on _glm_points is needed: the seed lookup and the
    // point delete both drive *from* this (small) table and probe the source /
    // destination endpoint indexes. ANALYZE so the planner knows it is small
    // and drives the joins from it.
    dbexec("ANALYZE _glm_points");

    // Step 2: Find the nodes (endpoint points) of every connected component
    // touched by the change. We seed from the lines that have a changed point
    // as one of their endpoints and walk out along shared endpoints, staying
    // within the same grouping key, until each connected component is fully
    // explored. The walk matches endpoints with exact geometry equality (so the
    // functional btree indexes on the endpoint points can be used) and
    // de-duplicates (group, point) pairs, which guarantees termination. All
    // groups meeting at a changed point are seeded, which keeps the by-point
    // delete in step 4 self-correcting (anything it removes that should survive
    // is regenerated). The 'where' filter is applied everywhere a source row is
    // read, so excluded lines neither seed nor extend a component. The grouping
    // values are carried under "gk_"-prefixed names so the unqualified 'where'
    // filter is never ambiguous against them.
    timer(m_timer_walk).start();
    dbexec(R"(
CREATE TEMP TABLE _glm_nodes ON COMMIT DROP AS
WITH RECURSIVE
seeds AS (
  -- The lines that touch a changed endpoint, found via the source endpoint
  -- index (exact equality, no tile-area scan).
  SELECT {group_cols_l}, l."{geom_column}"
    FROM _glm_points p
    JOIN {src} l
      ON ( ST_StartPoint(l."{geom_column}") = p.pt
        OR ST_EndPoint(l."{geom_column}") = p.pt )
     AND {where}
),
nodes AS (
  SELECT b.* FROM (
    SELECT {group_cols_gk}, ST_StartPoint("{geom_column}") AS pt FROM seeds
    UNION
    SELECT {group_cols_gk}, ST_EndPoint("{geom_column}") FROM seeds
  ) b
  UNION
  SELECT {group_cols_l_gk},
    CASE WHEN ST_StartPoint(l."{geom_column}") = n.pt
         THEN ST_EndPoint(l."{geom_column}")
         ELSE ST_StartPoint(l."{geom_column}") END
    FROM nodes n
    JOIN {src} l
      ON {group_join}
     AND ( ST_StartPoint(l."{geom_column}") = n.pt
        OR ST_EndPoint(l."{geom_column}") = n.pt )
     AND {where}
)
SELECT * FROM nodes
)");
    dbexec("ANALYZE _glm_nodes");

    // Step 3: Collect the actual member lines of those components: every source
    // line of the right group that touches a reached node at one of its
    // endpoints.
    auto const ways = dbexec(R"(
CREATE TEMP TABLE _glm_ways ON COMMIT DROP AS
SELECT DISTINCT ON (l.ctid) {group_cols_l}, l."{geom_column}"
  FROM _glm_nodes n
  JOIN {src} l
    ON {group_join}
   AND ( ST_StartPoint(l."{geom_column}") = n.pt
      OR ST_EndPoint(l."{geom_column}") = n.pt )
   AND {where}
 ORDER BY l.ctid
)");
    timer(m_timer_walk).stop();
    log_gen("Collected {} member lines in affected components.",
            ways.affected_rows());

    // Step 4: Delete the existing merged outputs of the touched components.
    // A merged output is part of a touched component if and only if one of its
    // endpoints coincides exactly with a reached node of the same group (the
    // walk only connects lines that share an endpoint exactly, so this matches
    // the walk's notion of connectivity and will not delete a different
    // component that merely crosses one mid-segment). This must use exact
    // endpoint equality, not ST_Intersects, to avoid deleting unrelated
    // same-group lines that cross a component. A second pass deletes any output
    // whose endpoint coincides with a changed point directly; this cleans up
    // components that disappeared entirely (all their lines deleted), which
    // leave no reached nodes behind. It is group-agnostic on purpose and stays
    // self-correcting because step 2 seeds every group meeting at a changed
    // point, so anything removed here that should survive is regenerated below.
    timer(m_timer_delete).start();
    auto deleted = dbexec(R"(
DELETE FROM {dest} d
 USING _glm_nodes n
 WHERE {group_join_dn}
   AND ( ST_StartPoint(d."{geom_column}") = n.pt
      OR ST_EndPoint(d."{geom_column}") = n.pt )
)");
    auto const deleted_by_nodes = deleted.affected_rows();
    deleted = dbexec(R"(
DELETE FROM {dest} d
 USING _glm_points p
 WHERE ST_StartPoint(d."{geom_column}") = p.pt
    OR ST_EndPoint(d."{geom_column}") = p.pt
)");
    timer(m_timer_delete).stop();
    log_gen("Deleted {} stale merged linestrings ({} by node, {} by point).",
            deleted_by_nodes + deleted.affected_rows(), deleted_by_nodes,
            deleted.affected_rows());

    // Step 5: Regenerate the affected components from scratch.
    timer(m_timer_merge).start();
    auto const inserted = dbexec(R"(
INSERT INTO {dest} ({group_cols}, "{geom_column}")
 SELECT {group_cols},
        (ST_Dump(ST_LineMerge(ST_Collect("{geom_column}"
            ORDER BY "{geom_column}")))).geom
   FROM _glm_ways
  GROUP BY {group_cols}
)");
    timer(m_timer_merge).stop();
    log_gen("Inserted {} merged linestrings.", inserted.affected_rows());

    connection().exec("COMMIT");
}
