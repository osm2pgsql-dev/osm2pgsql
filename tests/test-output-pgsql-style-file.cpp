/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

TEST_CASE("Parse default style file")
{
    export_list exlist;
    auto const enable_way_area =
        read_style_file(OSM2PGSQLDATA_DIR "default.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 98);
    REQUIRE(exlist.get(osmium::item_type::way).size() == 104);

}

TEST_CASE("Parse empty style file")
{
    export_list exlist;

    REQUIRE_THROWS_WITH(
        read_style_file(OSM2PGSQLDATA_DIR "tests/style/empty.style",
                        &exlist),
        "Unable to parse any valid columns from the style file. Aborting.");
}

TEST_CASE("Parse style file with invalid osm type")
{
    export_list exlist;

    REQUIRE_THROWS(read_style_file(
        OSM2PGSQLDATA_DIR "tests/style/invalid-osm-type.style", &exlist));
}

TEST_CASE("Parse style file with comments only")
{
    export_list exlist;

    REQUIRE_THROWS_WITH(
        read_style_file(OSM2PGSQLDATA_DIR "tests/style/comments.style",
                        &exlist),
        "Unable to parse any valid columns from the style file. Aborting.");
}

TEST_CASE("Parse style file with single node entry")
{
    export_list exlist;
    auto const enable_way_area =
        read_style_file(OSM2PGSQLDATA_DIR "tests/style/node.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 1);
    REQUIRE(exlist.get(osmium::item_type::way).empty());

    auto const &ex = exlist.get(osmium::item_type::node).front();
    REQUIRE(ex.name == "access");
    REQUIRE(ex.type == "text");
    REQUIRE(ex.flags == column_flags::FLAG_LINEAR);
    REQUIRE(ex.column_type() == ColumnType::TEXT);
}

TEST_CASE("Parse style file with a few valid entries")
{
    export_list exlist;
    auto const enable_way_area =
        read_style_file(OSM2PGSQLDATA_DIR "tests/style/valid.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 6);
    REQUIRE(exlist.get(osmium::item_type::way).size() == 6);

    auto const &nodes = exlist.get(osmium::item_type::node);
    auto const &ways = exlist.get(osmium::item_type::way);

    for (auto const &node : nodes) {
        REQUIRE(node.type == "text");
        REQUIRE(node.column_type() == ColumnType::TEXT);
    }

    for (auto const &way : ways) {
        REQUIRE(way.type == "text");
        REQUIRE(way.column_type() == ColumnType::TEXT);
    }

    REQUIRE(nodes[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(nodes[1].flags == column_flags::FLAG_LINEAR);
    REQUIRE(nodes[2].flags == column_flags::FLAG_POLYGON);
    REQUIRE(nodes[3].flags == column_flags::FLAG_POLYGON);
    REQUIRE(nodes[4].flags == column_flags::FLAG_NOCOLUMN);
    REQUIRE(nodes[5].flags == column_flags::FLAG_DELETE);
    REQUIRE(ways[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(ways[1].flags == column_flags::FLAG_LINEAR);
    REQUIRE(ways[2].flags == column_flags::FLAG_POLYGON);
    REQUIRE(ways[3].flags == column_flags::FLAG_POLYGON);
    REQUIRE(ways[4].flags == column_flags::FLAG_NOCOLUMN);
    REQUIRE(ways[5].flags == column_flags::FLAG_DELETE);
}

TEST_CASE("Parse style file with missing fields")
{
    export_list exlist;
    auto const enable_way_area =
        read_style_file(OSM2PGSQLDATA_DIR "tests/style/missing.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 2);
    REQUIRE(exlist.get(osmium::item_type::way).size() == 2);

    auto const &nodes = exlist.get(osmium::item_type::node);
    auto const &ways = exlist.get(osmium::item_type::way);

    for (auto const &node : nodes) {
        REQUIRE(node.type == "text");
        REQUIRE(node.column_type() == ColumnType::TEXT);
    }
    REQUIRE(nodes[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(nodes[1].flags == 0);

    for (auto const &way : ways) {
        REQUIRE(way.type == "text");
        REQUIRE(way.column_type() == ColumnType::TEXT);
    }
    REQUIRE(ways[0].flags == column_flags::FLAG_POLYGON);
    REQUIRE(ways[1].flags == 0);
}

TEST_CASE("Parse style file with way_area")
{
    export_list exlist;
    auto const enable_way_area = read_style_file(
        OSM2PGSQLDATA_DIR "tests/style/way-area.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 1);
    REQUIRE(exlist.get(osmium::item_type::way).size() == 2);

    auto const &nodes = exlist.get(osmium::item_type::node);
    auto const &ways = exlist.get(osmium::item_type::way);

    REQUIRE(nodes[0].type == "text");
    REQUIRE(nodes[0].flags ==
            (column_flags::FLAG_POLYGON | column_flags::FLAG_NOCOLUMN));
    REQUIRE(nodes[0].column_type() == ColumnType::TEXT);

    REQUIRE(ways[0].type == "text");
    REQUIRE(ways[0].flags ==
            (column_flags::FLAG_POLYGON | column_flags::FLAG_NOCOLUMN));
    REQUIRE(ways[0].column_type() == ColumnType::TEXT);

    REQUIRE(ways[1].type == "real");
    REQUIRE(ways[1].flags == 0);
    REQUIRE(ways[1].column_type() ==
            ColumnType::TEXT); // Special case for way_area!
}

TEST_CASE("Parse style file with different data types")
{
    export_list exlist;
    auto const enable_way_area = read_style_file(
        OSM2PGSQLDATA_DIR "tests/style/data-types.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).size() == 2);
    REQUIRE(exlist.get(osmium::item_type::way).size() == 3);

    auto const &nodes = exlist.get(osmium::item_type::node);
    auto const &ways = exlist.get(osmium::item_type::way);

    REQUIRE(nodes[0].name == "name");
    REQUIRE(nodes[0].type == "text");
    REQUIRE(nodes[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(nodes[0].column_type() == ColumnType::TEXT);

    REQUIRE(nodes[1].name == "population");
    REQUIRE(nodes[1].type == "integer");
    REQUIRE(nodes[1].flags ==
            (column_flags::FLAG_POLYGON | column_flags::FLAG_INT_TYPE));
    REQUIRE(nodes[1].column_type() == ColumnType::INT);

    REQUIRE(ways[0].name == "name");
    REQUIRE(ways[0].type == "text");
    REQUIRE(ways[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(ways[0].column_type() == ColumnType::TEXT);

    REQUIRE(ways[1].name == "width");
    REQUIRE(ways[1].type == "real");
    REQUIRE(ways[1].flags ==
            (column_flags::FLAG_LINEAR | column_flags::FLAG_REAL_TYPE));
    REQUIRE(ways[1].column_type() == ColumnType::REAL);

    REQUIRE(ways[2].name == "population");
    REQUIRE(ways[2].type == "integer");
    REQUIRE(ways[2].flags ==
            (column_flags::FLAG_POLYGON | column_flags::FLAG_INT_TYPE));
    REQUIRE(ways[2].column_type() == ColumnType::INT);
}

TEST_CASE("Parse style file with invalid data types")
{
    export_list exlist;
    auto const enable_way_area = read_style_file(
        OSM2PGSQLDATA_DIR "tests/style/invalid-data-type.style", &exlist);

    REQUIRE(enable_way_area);

    REQUIRE(exlist.get(osmium::item_type::node).empty());
    REQUIRE(exlist.get(osmium::item_type::way).size() == 1);

    auto const &ways = exlist.get(osmium::item_type::way);

    REQUIRE(ways[0].name == "highway");
    REQUIRE(ways[0].type == "foo");
    REQUIRE(ways[0].flags == column_flags::FLAG_LINEAR);
    REQUIRE(ways[0].column_type() == ColumnType::TEXT);
}
