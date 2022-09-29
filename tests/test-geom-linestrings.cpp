/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-buffer.hpp"

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-output.hpp"
#include "geom.hpp"

#include <array>

TEST_CASE("geom::linestring_t", "[NoDB]")
{
    geom::linestring_t ls1;

    REQUIRE(ls1.empty());
    ls1.emplace_back(17, 42);
    ls1.emplace_back(-3, 22);
    REQUIRE(ls1.size() == 2);

    auto it = ls1.cbegin();
    REQUIRE(it != ls1.cend());
    REQUIRE(it->x() == 17);
    ++it;
    REQUIRE(it != ls1.cend());
    REQUIRE(it->y() == 22);
    ++it;
    REQUIRE(it == ls1.cend());

    REQUIRE(ls1.num_geometries() == 1);
}

TEST_CASE("line geometry", "[NoDB]")
{
    geom::geometry_t const geom{geom::linestring_t{{1, 1}, {2, 2}}};

    REQUIRE(dimension(geom) == 1);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(1.41421));
    REQUIRE(geometry_type(geom) == "LINESTRING");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
    REQUIRE(geometry_n(geom, 1) == geom);
}

TEST_CASE("reverse line geometry", "[NoDB]")
{
    geom::geometry_t const geom{geom::linestring_t{{1, 1}, {2, 2}}};

    auto reversed = geom::reverse(geom);
    REQUIRE(num_geometries(reversed) == 1);
    REQUIRE(geometry_type(reversed) == "LINESTRING");

    auto const &line = reversed.get<geom::linestring_t>();
    REQUIRE(line.size() == 2);
    REQUIRE(line == geom::linestring_t{{2, 2}, {1, 1}});
}

TEST_CASE("create_linestring from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y2");

    auto const geom =
        geom::create_linestring(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_linestring());
    REQUIRE(geometry_type(geom) == "LINESTRING");
    REQUIRE(dimension(geom) == 1);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(1.41421));
    REQUIRE(geom.get<geom::linestring_t>() ==
            geom::linestring_t{{1, 1}, {2, 2}});
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("create_linestring from OSM data without locations", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1,n2");

    auto const geom =
        geom::create_linestring(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}

TEST_CASE("create_linestring from invalid OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1");

    auto const geom =
        geom::create_linestring(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}

TEST_CASE("geom::segmentize w/o split", "[NoDB]")
{
    geom::linestring_t const expected{{0, 0}, {1, 2}, {2, 2}};
    geom::linestring_t line = expected;

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 10.0);

    REQUIRE(geom.is_multilinestring());
    REQUIRE(num_geometries(geom) == 1);
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("geom::segmentize with split 0.5", "[NoDB]")
{
    geom::linestring_t line{{0, 0}, {1, 0}};

    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{0, 0}, {0.5, 0}},
        geom::linestring_t{{0.5, 0}, {1, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 0.5);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}

TEST_CASE("geom::segmentize with split 0.4", "[NoDB]")
{
    geom::linestring_t line{{0, 0}, {1, 0}};

    std::array<geom::linestring_t, 3> const expected{
        geom::linestring_t{{0, 0}, {0.4, 0}},
        geom::linestring_t{{0.4, 0}, {0.8, 0}},
        geom::linestring_t{{0.8, 0}, {1, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 0.4);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 3);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
}

TEST_CASE("geom::segmentize with split 1.0 at start", "[NoDB]")
{
    geom::linestring_t line{{0, 0}, {2, 0}, {3, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 1.0);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 4);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
    REQUIRE(ml[3] == expected[3]);
}

TEST_CASE("geom::segmentize with split 1.0 in middle", "[NoDB]")
{
    geom::linestring_t line{{0, 0}, {1, 0}, {3, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 1.0);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 4);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
    REQUIRE(ml[3] == expected[3]);
}

TEST_CASE("geom::segmentize with split 1.0 at end", "[NoDB]")
{
    geom::linestring_t line{{0, 0}, {1, 0}, {2, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{std::move(line)}, 1.0);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 4);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
    REQUIRE(ml[3] == expected[3]);
}

TEST_CASE("geom::simplify", "[NoDB]")
{
    geom::geometry_t const input{
        geom::linestring_t{{0, 0}, {1, 1}, {2, 0}, {3, 1}, {4, 0}, {5, 1}}};

    SECTION("small tolerance leaves linestring as is")
    {
        auto const geom = geom::simplify(input, 0.5);

        REQUIRE(geom.is_linestring());
        auto const &l = geom.get<geom::linestring_t>();
        REQUIRE(l.size() == 6);
        REQUIRE(l == input.get<geom::linestring_t>());
    }

    SECTION("large tolerance simplifies linestring")
    {
        auto const geom = geom::simplify(input, 10.0);

        REQUIRE(geom.is_linestring());
        auto const &l = geom.get<geom::linestring_t>();
        REQUIRE(l.size() == 2);
        REQUIRE(l[0] == input.get<geom::linestring_t>()[0]);
        REQUIRE(l[1] == input.get<geom::linestring_t>()[5]);
    }
}

TEST_CASE("geom::simplify of a loop", "[NoDB]")
{
    geom::geometry_t const input{
        geom::linestring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0.1, 0.1}, {0, 0}}};

    SECTION("small tolerance leaves linestring as is")
    {
        auto const geom = geom::simplify(input, 0.01);

        REQUIRE(geom.is_linestring());
        auto const &l = geom.get<geom::linestring_t>();
        REQUIRE(l.size() == 6);
        REQUIRE(l == input.get<geom::linestring_t>());
    }

    SECTION("medium tolerance simplifies linestring")
    {
        auto const geom = geom::simplify(input, 0.5);

        REQUIRE(geom.is_linestring());
        auto const &l = geom.get<geom::linestring_t>();
        REQUIRE(l.size() == 5);

        auto const &il = input.get<geom::linestring_t>();
        REQUIRE(l[0] == il[0]);
        REQUIRE(l[1] == il[1]);
        REQUIRE(l[2] == il[2]);
        REQUIRE(l[3] == il[3]);
        REQUIRE(l[4] == il[5]);
    }

    SECTION("large tolerance breaks linestring, null geometry is returned")
    {
        auto const geom = geom::simplify(input, 10.0);

        REQUIRE(geom.is_null());
    }
}
