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

#include "geom.hpp"
#include "reprojection.hpp"

#include <array>

using Coordinates = osmium::geom::Coordinates;

TEST_CASE("geom::distance", "[NoDB]")
{
    Coordinates const p1{10, 10};
    Coordinates const p2{20, 10};
    Coordinates const p3{13, 14};

    REQUIRE(geom::distance(p1, p1) == Approx(0.0));
    REQUIRE(geom::distance(p1, p2) == Approx(10.0));
    REQUIRE(geom::distance(p1, p3) == Approx(5.0));
}

TEST_CASE("geom::interpolate", "[NoDB]")
{
    Coordinates const p1{10, 10};
    Coordinates const p2{20, 10};

    auto const i1 = geom::interpolate(p1, p1, 0.5);
    REQUIRE(i1.x == 10);
    REQUIRE(i1.y == 10);

    auto const i2 = geom::interpolate(p1, p2, 0.5);
    REQUIRE(i2.x == 15);
    REQUIRE(i2.y == 10);

    auto const i3 = geom::interpolate(p2, p1, 0.5);
    REQUIRE(i3.x == 15);
    REQUIRE(i3.y == 10);
}

TEST_CASE("geom::linestring_t", "[NoDB]")
{
    geom::linestring_t ls1;

    REQUIRE(ls1.empty());
    ls1.add_point(Coordinates{17, 42});
    ls1.add_point(Coordinates{-3, 22});
    REQUIRE(!ls1.empty());
    REQUIRE(ls1.size() == 2);

    auto it = ls1.cbegin();
    REQUIRE(it != ls1.cend());
    REQUIRE(it->x == 17);
    ++it;
    REQUIRE(it != ls1.cend());
    REQUIRE(it->y == 22);
    ++it;
    REQUIRE(it == ls1.cend());
}

TEST_CASE("geom::split_linestring w/o split", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 2},
                                  Coordinates{2, 2}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 10.0, &result);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == line);
}

TEST_CASE("geom::split_linestring with split 0.5", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0}};

    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{0.5, 0}},
        geom::linestring_t{Coordinates{0.5, 0}, Coordinates{1, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 0.5, &result);

    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
}

TEST_CASE("geom::split_linestring with split 0.4", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0}};

    std::array<geom::linestring_t, 3> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{0.4, 0}},
        geom::linestring_t{Coordinates{0.4, 0}, Coordinates{0.8, 0}},
        geom::linestring_t{Coordinates{0.8, 0}, Coordinates{1, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 0.4, &result);

    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
}

TEST_CASE("geom::split_linestring with split 1.0 at start", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{2, 0},
                                  Coordinates{3, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

TEST_CASE("geom::split_linestring with split 1.0 in middle", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0},
                                  Coordinates{3, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

TEST_CASE("geom::split_linestring with split 1.0 at end", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0},
                                  Coordinates{2, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

TEST_CASE("make_multiline with single line", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 1}, Coordinates{2, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline with single line forming a ring", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 1}, Coordinates{2, 1},
                                      Coordinates{2, 2}, Coordinates{1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n12x2y2,n10x1y1");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from two non-joined lines", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{1, 1}, Coordinates{2, 1}},
        geom::linestring_t{Coordinates{2, 2}, Coordinates{3, 2}}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn12x2y2,n13x3y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == expected[0]);
    REQUIRE(lines[1] == expected[1]);
}

TEST_CASE("make_multiline from two lines end to end", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 1}, Coordinates{2, 1},
                                      Coordinates{2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn11x2y1,n12x2y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from two lines with same start point", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{2, 1}, Coordinates{1, 1},
                                      Coordinates{1, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from two lines with same end point", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 2}, Coordinates{1, 1},
                                      Coordinates{2, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y2,n11x1y1");
    buffer.add_way("w21 Nn12x2y1,n11x1y1");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from two lines connected end to end forming a ring", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 1}, Coordinates{2, 1},
                                      Coordinates{2, 2}, Coordinates{1, 2},
                                      Coordinates{1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn13x2y2,n12x1y2,n10x1y1");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from two lines with same start and end point", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{2, 2}, Coordinates{2, 1},
                                      Coordinates{1, 1}, Coordinates{1, 2},
                                      Coordinates{2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn10x1y1,n12x1y2,n13x2y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from three lines, two with same start and end point", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{2, 2}, Coordinates{2, 1},
                                      Coordinates{1, 1}, Coordinates{1, 2},
                                      Coordinates{2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn12x1y2,n13x2y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from four lines forming two rings", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{2, 1}, Coordinates{1, 1},
                           Coordinates{1, 2}},
        geom::linestring_t{Coordinates{3, 4}, Coordinates{3, 3},
                           Coordinates{4, 3}}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn13x3y4,n14x3y3");
    buffer.add_way("w23 Nn15x4y3,n14x3y3");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == expected[0]);
    REQUIRE(lines[1] == expected[1]);
}

TEST_CASE("make_multiline from Y shape", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{2, 1}, Coordinates{1, 1},
                           Coordinates{1, 2}},
        geom::linestring_t{
            Coordinates{1, 1},
            Coordinates{2, 2},
        }};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn10x1y1,n13x2y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == expected[0]);
    REQUIRE(lines[1] == expected[1]);
}

TEST_CASE("make_multiline from P shape", "[NoDB]")
{
    geom::linestring_t const expected{Coordinates{1, 1}, Coordinates{1, 2},
                                      Coordinates{1, 3}, Coordinates{2, 3},
                                      Coordinates{1, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x1y2,n12x1y3");
    buffer.add_way("w21 Nn12x1y3,n13x2y3,n11x1y2");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == expected);
}

TEST_CASE("make_multiline from P shape with closed way", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{1, 2}, Coordinates{1, 1}},
        geom::linestring_t{
            Coordinates{1, 2},
            Coordinates{1, 3},
            Coordinates{2, 3},
            Coordinates{1, 2},
        }};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn11x1y2,n12x1y3,n13x2y3,n11x1y2");
    buffer.add_way("w21 Nn11x1y2,n10x1y1");

    std::vector<geom::linestring_t> lines;

    auto const proj = reprojection::create_projection(4326);
    geom::make_multiline(buffer.buffer(), 0.0, *proj, &lines);

    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == expected[0]);
    REQUIRE(lines[1] == expected[1]);
}

