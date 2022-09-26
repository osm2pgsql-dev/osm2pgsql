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

#include <array>

TEST_CASE("create_multilinestring with single line", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}};
    geom::linestring_t const expected_rev{{2, 1}, {1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    REQUIRE(geometry_type(geom) == "MULTILINESTRING");
    REQUIRE(dimension(geom) == 1);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(1.0));
    auto const &ml = geom.get<geom::multilinestring_t>();
    REQUIRE(ml.num_geometries() == 1);
    REQUIRE(ml[0] == expected);

    auto const rev = reverse(geom);
    REQUIRE(rev.is_multilinestring());
    REQUIRE(rev.get<geom::multilinestring_t>()[0] == expected_rev);
}

TEST_CASE("create_multilinestring with single line and no force_multi",
          "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer(), false));

    REQUIRE(geom.is_linestring());
    REQUIRE(geometry_type(geom) == "LINESTRING");
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(1.0));
    auto const &l = geom.get<geom::linestring_t>();
    REQUIRE(l.num_geometries() == 1);
    REQUIRE(l == expected);
}

TEST_CASE("create_multilinestring with single line forming a ring", "[NoDB]")
{
    geom::linestring_t const expected{{1, 1}, {2, 1}, {2, 2}, {1, 1}};

    test_buffer_t buffer;
    buffer.add_way("w20 Nn10x1y1,n11x2y1,n12x2y2,n10x1y1");

    auto const geom =
        geom::line_merge(geom::create_multilinestring(buffer.buffer()));

    REQUIRE(geom.is_multilinestring());
    REQUIRE(dimension(geom) == 1);
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
    REQUIRE(dimension(geom) == 1);
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

TEST_CASE("create_multilinestring from four segments forming two lines",
          "[NoDB]")
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
    REQUIRE(geometry_n(geom, 1).get<geom::linestring_t>() == expected[0]);
    REQUIRE(geometry_n(geom, 2).get<geom::linestring_t>() == expected[1]);
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
