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
#include "geom.hpp"
#include "reprojection.hpp"

#include <array>

TEST_CASE("geom::distance", "[NoDB]")
{
    geom::point_t const p1{10, 10};
    geom::point_t const p2{20, 10};
    geom::point_t const p3{13, 14};

    REQUIRE(geom::distance(p1, p1) == Approx(0.0));
    REQUIRE(geom::distance(p1, p2) == Approx(10.0));
    REQUIRE(geom::distance(p1, p3) == Approx(5.0));
}

TEST_CASE("geom::interpolate", "[NoDB]")
{
    geom::point_t const p1{10, 10};
    geom::point_t const p2{20, 10};

    auto const i1 = geom::interpolate(p1, p1, 0.5);
    REQUIRE(i1.x() == 10);
    REQUIRE(i1.y() == 10);

    auto const i2 = geom::interpolate(p1, p2, 0.5);
    REQUIRE(i2.x() == 15);
    REQUIRE(i2.y() == 10);

    auto const i3 = geom::interpolate(p2, p1, 0.5);
    REQUIRE(i3.x() == 15);
    REQUIRE(i3.y() == 10);
}

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
}

TEST_CASE("geom::segmentize w/o split", "[NoDB]")
{
    geom::linestring_t const line{{0, 0}, {1, 2}, {2, 2}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 10.0);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == line);
}

TEST_CASE("geom::segmentize with split 0.5", "[NoDB]")
{
    geom::linestring_t const line{{0, 0}, {1, 0}};

    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{0, 0}, {0.5, 0}},
        geom::linestring_t{{0.5, 0}, {1, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 0.5);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}

TEST_CASE("geom::segmentize with split 0.4", "[NoDB]")
{
    geom::linestring_t const line{{0, 0}, {1, 0}};

    std::array<geom::linestring_t, 3> const expected{
        geom::linestring_t{{0, 0}, {0.4, 0}},
        geom::linestring_t{{0.4, 0}, {0.8, 0}},
        geom::linestring_t{{0.8, 0}, {1, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 0.4);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 3);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
}

TEST_CASE("geom::segmentize with split 1.0 at start", "[NoDB]")
{
    geom::linestring_t const line{{0, 0}, {2, 0}, {3, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 1.0);

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
    geom::linestring_t const line{{0, 0}, {1, 0}, {3, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 1.0);

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
    geom::linestring_t const line{{0, 0}, {1, 0}, {2, 0}, {4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{{0, 0}, {1, 0}}, geom::linestring_t{{1, 0}, {2, 0}},
        geom::linestring_t{{2, 0}, {3, 0}}, geom::linestring_t{{3, 0}, {4, 0}}};

    auto const geom = geom::segmentize(geom::geometry_t{line}, 1.0);

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 4);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
    REQUIRE(ml[2] == expected[2]);
    REQUIRE(ml[3] == expected[3]);
}

TEST_CASE("create_multilinestring with single line", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring with single line forming a ring", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}, {2, 2}, {1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n12x2y2,n10x1y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from two non-joined lines", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{1, 1}, {2, 1}}, geom::linestring_t{{2, 2}, {3, 2}}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn12x2y2,n13x3y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}

TEST_CASE("create_multilinestring from two lines end to end", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}, {2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn11x2y1,n12x2y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from two lines with same start point",
          "[NoDB]")
{
    geom::linestring_t const expected{{2, 1}, {1, 1}, {1, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from two lines with same end point", "[NoDB]")
{
    geom::linestring_t const expected{{1, 2}, {1, 1}, {2, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y2,n11x1y1");
    buffer.add_way("w21 Nn12x2y1,n11x1y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from two lines connected end to end forming "
          "a ring",
          "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn13x2y2,n12x1y2,n10x1y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from two lines with same start and end point",
          "[NoDB]")
{
    geom::linestring_t const expected{{2, 2}, {2, 1}, {1, 1}, {1, 2}, {2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn10x1y1,n12x1y2,n13x2y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from three lines, two with same start and "
          "end point",
          "[NoDB]")
{
    geom::linestring_t const expected{{2, 2}, {2, 1}, {1, 1}, {1, 2}, {2, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n13x2y2");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn12x1y2,n13x2y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from four lines forming two rings", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{2, 1}, {1, 1}, {1, 2}},
        geom::linestring_t{{3, 4}, {3, 3}, {4, 3}}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn13x3y4,n14x3y3");
    buffer.add_way("w23 Nn15x4y3,n14x3y3");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}

TEST_CASE("create_multilinestring from Y shape", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{2, 1}, {1, 1}, {1, 2}}, geom::linestring_t{
                                                        {1, 1},
                                                        {2, 2},
                                                    }};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");
    buffer.add_way("w21 Nn10x1y1,n12x1y2");
    buffer.add_way("w22 Nn10x1y1,n13x2y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}

TEST_CASE("create_multilinestring from P shape", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {1, 2}, {1, 3}, {2, 3}, {1, 2}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x1y2,n12x1y3");
    buffer.add_way("w21 Nn12x1y3,n13x2y3,n11x1y2");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);
}

TEST_CASE("create_multilinestring from P shape with closed way", "[NoDB]")
{
    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{{1, 2}, {1, 1}}, geom::linestring_t{
                                                {1, 2},
                                                {1, 3},
                                                {2, 3},
                                                {1, 2},
                                            }};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn11x1y2,n12x1y3,n13x2y3,n11x1y2");
    buffer.add_way("w21 Nn11x1y2,n10x1y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 2);
    REQUIRE(ml[0] == expected[0]);
    REQUIRE(ml[1] == expected[1]);
}
