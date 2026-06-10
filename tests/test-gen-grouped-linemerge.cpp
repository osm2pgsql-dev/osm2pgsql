/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-pg.hpp"
#include "format.hpp"
#include "gen/gen-grouped-linemerge.hpp"
#include "params.hpp"
#include "pgsql.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

testing::pg::tempdb_t db;

// Run the grouped-linemerge strategy (create or append) against the test
// tables. conn_t is a pg_conn_t, so it can be passed straight to the strategy.
void run_gen(testing::pg::conn_t &conn, bool append,
             char const *src_table = "glm_lines",
             char const *dest_table = "glm_merged",
             char const *group_by_columns = "grp",
             char const *where = nullptr)
{
    params_t params;
    params.set("schema", "public");
    params.set("src_table", src_table);
    params.set("dest_table", dest_table);
    params.set("geom_column", "geom");
    params.set("group_by_columns", group_by_columns);
    if (where != nullptr) {
        params.set("where", where);
    }
    params.set("endpoint_table", "glm_exp");

    gen_grouped_linemerge_t gen{&conn, append, &params};
    gen.process();
}

void setup_tables(testing::pg::conn_t &conn)
{
    conn.exec("DROP TABLE IF EXISTS glm_lines, glm_merged, glm_exp, glm_ref"
              " CASCADE");
    conn.exec("CREATE TABLE glm_lines"
              " (grp text, geom geometry(LineString, 3857) NOT NULL)");
    conn.exec("CREATE INDEX ON glm_lines USING gist (geom)");
    conn.exec("CREATE TABLE glm_merged (grp text, geom geometry NOT NULL)");
    conn.exec("CREATE INDEX ON glm_merged USING gist (geom)");
    // The change signal: the exact endpoints of changed ways (what osm2pgsql's
    // expire output with an 'endpoint_table' writes during an update).
    conn.exec("CREATE TABLE glm_exp (geom geometry(Point, 3857) NOT NULL)");
}

void insert_edge(testing::pg::conn_t &conn, std::string const &grp,
                 std::string const &wkt)
{
    conn.exec(fmt::format("INSERT INTO glm_lines (grp, geom)"
                          " VALUES ('{}', ST_GeomFromText('{}', 3857))",
                          grp, wkt));
}

void delete_edge(testing::pg::conn_t &conn, std::string const &grp,
                 std::string const &wkt)
{
    conn.exec(fmt::format("DELETE FROM glm_lines WHERE grp = '{}'"
                          " AND ST_Equals(geom, ST_GeomFromText('{}', 3857))",
                          grp, wkt));
}

// Record the exact endpoints of a changed way, exactly as osm2pgsql's expire
// output with an 'endpoint_table' would during an update (start and end point
// of the changed geometry).
void expire(testing::pg::conn_t &conn, std::string const &wkt)
{
    conn.exec(fmt::format(
        "INSERT INTO glm_exp (geom)"
        " VALUES (ST_StartPoint(ST_GeomFromText('{0}', 3857))),"
        "        (ST_EndPoint(ST_GeomFromText('{0}', 3857)))",
        wkt));
}

// Whether the strategy's output equals what a from-scratch GROUP BY +
// ST_LineMerge would produce on the current source. Compares as a set
// (geometric equality, group-aware, NULL-safe) AND on row count, so leftover
// duplicates or missing pieces are both caught.
bool matches_reference(testing::pg::conn_t &conn)
{
    conn.exec("DROP TABLE IF EXISTS glm_ref");
    conn.exec("CREATE TABLE glm_ref AS"
              " SELECT grp, (ST_Dump(ST_LineMerge(ST_Collect(geom)))).geom AS geom"
              " FROM glm_lines GROUP BY grp");

    int const ref = conn.result_as_int("SELECT count(*) FROM glm_ref");
    int const strat = conn.result_as_int("SELECT count(*) FROM glm_merged");
    int const strat_extra = conn.result_as_int(
        "SELECT count(*) FROM glm_merged m WHERE NOT EXISTS (SELECT 1 FROM"
        " glm_ref r WHERE r.grp IS NOT DISTINCT FROM m.grp"
        " AND ST_Equals(r.geom, m.geom))");
    int const ref_extra = conn.result_as_int(
        "SELECT count(*) FROM glm_ref r WHERE NOT EXISTS (SELECT 1 FROM"
        " glm_merged m WHERE r.grp IS NOT DISTINCT FROM m.grp"
        " AND ST_Equals(r.geom, m.geom))");

    INFO("reference=" << ref << " strategy=" << strat
                      << " strategy_only=" << strat_extra
                      << " reference_only=" << ref_extra);
    return ref == strat && strat_extra == 0 && ref_extra == 0;
}

struct edge_t
{
    std::string wkt;      // straight variant
    std::string wkt_bent; // same endpoints, detour through an offset midpoint
    bool present = false;
    bool bent = false;
    std::string grp;

    edge_t(std::string w, std::string wb)
    : wkt(std::move(w)), wkt_bent(std::move(wb))
    {}

    std::string const &cur_wkt() const noexcept { return bent ? wkt_bent : wkt; }
};

// All horizontal and vertical segments of a GW x GH grid (the candidate
// "ways"). Interior grid points become degree-3+ junctions when same-group
// edges meet there, which is exactly the case that makes ST_LineMerge split a
// connected component into several output lines. Each edge also has a "bent"
// variant with the same endpoints but a different interior, to simulate a way
// whose geometry changes without its endpoints moving.
std::vector<edge_t> build_grid_edges()
{
    constexpr int GW = 4;
    constexpr int GH = 4;
    constexpr int STEP = 500;
    std::vector<edge_t> edges;
    auto const seg = [](int x1, int y1, int x2, int y2) {
        return fmt::format("LINESTRING({} {},{} {})", x1, y1, x2, y2);
    };
    auto const seg_bent = [](int x1, int y1, int x2, int y2) {
        // The offset midpoint never coincides with a grid point.
        return fmt::format("LINESTRING({} {},{} {},{} {})", x1, y1,
                           (x1 + x2) / 2 + 123, (y1 + y2) / 2 + 77, x2, y2);
    };
    for (int j = 0; j < GH; ++j) {
        for (int i = 0; i < GW - 1; ++i) {
            edges.emplace_back(
                seg(i * STEP, j * STEP, (i + 1) * STEP, j * STEP),
                seg_bent(i * STEP, j * STEP, (i + 1) * STEP, j * STEP));
        }
    }
    for (int i = 0; i < GW; ++i) {
        for (int j = 0; j < GH - 1; ++j) {
            edges.emplace_back(
                seg(i * STEP, j * STEP, i * STEP, (j + 1) * STEP),
                seg_bent(i * STEP, j * STEP, i * STEP, (j + 1) * STEP));
        }
    }
    return edges;
}

constexpr std::array<char const *, 3> GROUPS = {"a", "b", "c"};

} // anonymous namespace

TEST_CASE("grouped-linemerge: create merges connected same-group lines")
{
    auto conn = db.connect();
    setup_tables(conn);

    // "a": two connected segments; "b": one segment touching them but in a
    // different group (must not merge); plus a disjoint "a" segment far away.
    insert_edge(conn, "a", "LINESTRING(0 0,500 0)");
    insert_edge(conn, "a", "LINESTRING(500 0,1000 0)");
    insert_edge(conn, "b", "LINESTRING(500 0,500 500)");
    insert_edge(conn, "a", "LINESTRING(5000 0,5500 0)");

    run_gen(conn, false);

    REQUIRE(matches_reference(conn));
    // "a" -> merged (0..1000) + disjoint (5000..5500) = 2 rows; "b" -> 1 row.
    CHECK(conn.get_count("glm_merged") == 3);
    CHECK(conn.get_count("glm_merged", "grp = 'a'") == 2);
}

TEST_CASE("grouped-linemerge: incremental updates match full re-merge (fuzz)")
{
    auto conn = db.connect();

    constexpr int BATCHES_PER_SEED = 60;

    for (unsigned const seed : {1U, 2U, 3U, 4U}) {
        setup_tables(conn);
        auto edges = build_grid_edges();
        std::mt19937 rng{seed};

        // Random initial population, then a from-scratch build.
        for (auto &e : edges) {
            if (rng() % 2U == 0U) {
                e.present = true;
                e.bent = (rng() % 2U == 0U);
                e.grp = GROUPS.at(rng() % 3U);
                insert_edge(conn, e.grp, e.cur_wkt());
            }
        }
        run_gen(conn, false);
        INFO("seed=" << seed << " phase=create");
        REQUIRE(matches_reference(conn));

        // Random batches of edits (like a real change file with several
        // changed ways), each batch followed by one incremental append that
        // must reproduce the from-scratch result. Edit kinds: add a line,
        // delete a line, reroute a line (same endpoints, new interior
        // geometry) and retag a line (same geometry, new group).
        for (int batch = 0; batch < BATCHES_PER_SEED; ++batch) {
            auto const num_ops = 1U + rng() % 4U;
            std::string desc;
            for (unsigned n = 0; n < num_ops; ++n) {
                std::vector<std::size_t> present;
                std::vector<std::size_t> absent;
                for (std::size_t i = 0; i < edges.size(); ++i) {
                    (edges[i].present ? present : absent).push_back(i);
                }

                auto const kind = rng() % 4U;
                if (present.empty() || (kind == 0U && !absent.empty())) {
                    auto const idx = absent[rng() % absent.size()];
                    auto &e = edges[idx];
                    e.grp = GROUPS.at(rng() % 3U);
                    e.bent = (rng() % 2U == 0U);
                    e.present = true;
                    insert_edge(conn, e.grp, e.cur_wkt());
                    expire(conn, e.cur_wkt());
                    desc += fmt::format(" add:{}/{}", idx, e.grp);
                } else if (kind == 1U) {
                    auto const idx = present[rng() % present.size()];
                    auto &e = edges[idx];
                    expire(conn, e.cur_wkt()); // the old footprint...
                    delete_edge(conn, e.grp, e.cur_wkt());
                    e.present = false;
                    desc += fmt::format(" del:{}/{}", idx, e.grp);
                } else if (kind == 2U) {
                    // reroute: the geometry changes, the endpoints stay
                    auto const idx = present[rng() % present.size()];
                    auto &e = edges[idx];
                    expire(conn, e.cur_wkt()); // the old geometry...
                    delete_edge(conn, e.grp, e.cur_wkt());
                    e.bent = !e.bent;
                    insert_edge(conn, e.grp, e.cur_wkt());
                    expire(conn, e.cur_wkt()); // ...and the new geometry
                    desc += fmt::format(" reroute:{}/{}", idx, e.grp);
                } else {
                    // retag: the group changes, the geometry stays
                    auto const idx = present[rng() % present.size()];
                    auto &e = edges[idx];
                    delete_edge(conn, e.grp, e.cur_wkt());
                    e.grp = GROUPS.at(rng() % 3U); // may even be unchanged
                    insert_edge(conn, e.grp, e.cur_wkt());
                    expire(conn, e.cur_wkt()); // old and new endpoints coincide
                    desc += fmt::format(" retag:{}/{}", idx, e.grp);
                }
            }

            run_gen(conn, true);

            INFO("seed=" << seed << " batch=" << batch << desc);
            REQUIRE(matches_reference(conn));
        }
    }
}

TEST_CASE("grouped-linemerge: multi-column grouping, NULL keys, where filter")
{
    auto conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS glm2_lines, glm2_merged, glm_exp, glm2_ref"
              " CASCADE");
    conn.exec("CREATE TABLE glm2_lines"
              " (name text, ref text, geom geometry(LineString, 3857) NOT NULL)");
    conn.exec("CREATE INDEX ON glm2_lines USING gist (geom)");
    conn.exec("CREATE TABLE glm2_merged"
              " (name text, ref text, geom geometry NOT NULL)");
    conn.exec("CREATE INDEX ON glm2_merged USING gist (geom)");
    conn.exec("CREATE TABLE glm_exp (geom geometry(Point, 3857) NOT NULL)");

    char const *const cols = "name, ref";
    char const *const filter = "(name IS NOT NULL OR ref IS NOT NULL)";

    auto ins = [&](char const *name, char const *ref, std::string const &wkt) {
        conn.exec(fmt::format(
            "INSERT INTO glm2_lines (name, ref, geom) VALUES ({}, {},"
            " ST_GeomFromText('{}', 3857))",
            name ? fmt::format("'{}'", name) : "NULL",
            ref ? fmt::format("'{}'", ref) : "NULL", wkt));
    };

    // The reference applies the same filter and multi-column grouping; the
    // match is group-aware (NULL-safe) and geometric.
    auto matches = [&]() {
        conn.exec("DROP TABLE IF EXISTS glm2_ref");
        conn.exec(fmt::format(
            "CREATE TABLE glm2_ref AS SELECT {0},"
            " (ST_Dump(ST_LineMerge(ST_Collect(geom)))).geom AS geom"
            " FROM glm2_lines WHERE {1} GROUP BY {0}",
            cols, filter));
        int const ref = conn.result_as_int("SELECT count(*) FROM glm2_ref");
        int const strat = conn.result_as_int("SELECT count(*) FROM glm2_merged");
        int const se = conn.result_as_int(
            "SELECT count(*) FROM glm2_merged m WHERE NOT EXISTS (SELECT 1 FROM"
            " glm2_ref r WHERE r.name IS NOT DISTINCT FROM m.name"
            " AND r.ref IS NOT DISTINCT FROM m.ref AND ST_Equals(r.geom, m.geom))");
        int const re = conn.result_as_int(
            "SELECT count(*) FROM glm2_ref r WHERE NOT EXISTS (SELECT 1 FROM"
            " glm2_merged m WHERE r.name IS NOT DISTINCT FROM m.name"
            " AND r.ref IS NOT DISTINCT FROM m.ref AND ST_Equals(r.geom, m.geom))");
        INFO("reference=" << ref << " strategy=" << strat << " strategy_only="
                          << se << " reference_only=" << re);
        return ref == strat && se == 0 && re == 0;
    };

    // 'Main St'/NULL: three connected segments -> one line.
    ins("Main St", nullptr, "LINESTRING(0 0,500 0)");
    ins("Main St", nullptr, "LINESTRING(500 0,1000 0)");
    ins("Main St", nullptr, "LINESTRING(1000 0,1500 0)");
    // NULL/'I 5': two connected segments -> one line (NULL name groups).
    ins(nullptr, "I 5", "LINESTRING(0 500,500 500)");
    ins(nullptr, "I 5", "LINESTRING(500 500,1000 500)");
    // NULL/NULL: excluded by the filter entirely.
    ins(nullptr, nullptr, "LINESTRING(0 1000,500 1000)");
    // 'Main St'/'I 5': a distinct group (differs from 'Main St'/NULL on ref).
    ins("Main St", "I 5", "LINESTRING(0 1500,500 1500)");

    run_gen(conn, false, "glm2_lines", "glm2_merged", cols, filter);
    INFO("phase=create");
    REQUIRE(matches());
    CHECK(conn.get_count("glm2_merged", "name IS NULL AND ref IS NULL") == 0);
    CHECK(conn.get_count("glm2_merged", "name = 'Main St' AND ref IS NULL") == 1);

    // Incremental: remove the middle 'Main St'/NULL segment; the component must
    // split into two, with everything else untouched.
    expire(conn, "LINESTRING(500 0,1000 0)");
    conn.exec("DELETE FROM glm2_lines WHERE name = 'Main St' AND ref IS NULL"
              " AND ST_Equals(geom, ST_GeomFromText('LINESTRING(500 0,1000 0)',"
              " 3857))");
    run_gen(conn, true, "glm2_lines", "glm2_merged", cols, filter);
    INFO("phase=append-shatter");
    REQUIRE(matches());
    CHECK(conn.get_count("glm2_merged", "name = 'Main St' AND ref IS NULL") == 2);
}
