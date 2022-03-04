/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "geom-box.hpp"

TEST_CASE("Extend box_t with points", "[NoDB]")
{
    geom::box_t box;

    box.extend(geom::point_t{1.0, 2.0});

    REQUIRE(box.min_x() == Approx(1.0));
    REQUIRE(box.max_x() == Approx(1.0));
    REQUIRE(box.min_y() == Approx(2.0));
    REQUIRE(box.max_y() == Approx(2.0));

    REQUIRE(box.width() == Approx(0.0));
    REQUIRE(box.height() == Approx(0.0));

    box.extend(geom::point_t{3.0, -2.0});

    REQUIRE(box.min_x() == Approx(1.0));
    REQUIRE(box.max_x() == Approx(3.0));
    REQUIRE(box.min_y() == Approx(-2.0));
    REQUIRE(box.max_y() == Approx(2.0));

    REQUIRE(box.width() == Approx(2.0));
    REQUIRE(box.height() == Approx(4.0));
}

TEST_CASE("Extend box_t with linestring", "[NoDB]")
{
    geom::box_t box;

    geom::linestring_t const ls{{1.0, 2.0}, {2.0, 2.0}, {-5.0, 3.0}};

    box.extend(ls);

    REQUIRE(box.min_x() == Approx(-5.0));
    REQUIRE(box.max_x() == Approx(2.0));
    REQUIRE(box.min_y() == Approx(2.0));
    REQUIRE(box.max_y() == Approx(3.0));

    REQUIRE(box.width() == Approx(7.0));
    REQUIRE(box.height() == Approx(1.0));
}

TEST_CASE("Calculate envelope of null geometry")
{
    geom::geometry_t const geom{};
    REQUIRE(geom::envelope(geom) == geom::box_t{});
}

TEST_CASE("Calculate envelope of point geometry")
{
    geom::geometry_t const geom{geom::point_t{2.3, 1.4}};
    REQUIRE(geom::envelope(geom) == geom::box_t{2.3, 1.4, 2.3, 1.4});
}

TEST_CASE("Calculate envelope of linestring geometry")
{
    geom::geometry_t const geom{geom::linestring_t{{2.3, 1.4}, {2.5, 1.0}}};
    REQUIRE(geom::envelope(geom) == geom::box_t{2.3, 1.0, 2.5, 1.4});
}

TEST_CASE("Calculate envelope of polygon geometry")
{
    geom::geometry_t const geom{geom::polygon_t{geom::ring_t{
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}, {0.0, 0.0}}}};
    REQUIRE(geom::envelope(geom) == geom::box_t{0.0, 0.0, 1.0, 1.0});
}

TEST_CASE("Calculate envelope of multilinestring geometry")
{
    geom::geometry_t geom{geom::multilinestring_t{}};

    auto &mls = geom.get<geom::multilinestring_t>();

    mls.add_geometry(geom::linestring_t{{2.3, 1.4}, {2.5, 1.0}});
    mls.add_geometry(geom::linestring_t{{7.3, 0.4}, {2.4, 1.8}});

    REQUIRE(geom::envelope(geom) == geom::box_t{2.3, 0.4, 7.3, 1.8});
}

TEST_CASE("Calculate envelope of multipolygon geometry")
{
    geom::geometry_t geom{geom::multipolygon_t{}};

    auto &mp = geom.get<geom::multipolygon_t>();

    mp.add_geometry(geom::polygon_t{geom::ring_t{
        {1.1, 1.1}, {1.1, 3.3}, {2.2, 3.3}, {2.2, 1.1}, {1.1, 1.1}}});
    mp.add_geometry(geom::polygon_t{geom::ring_t{
        {2.2, 2.2}, {2.2, 3.3}, {4.4, 3.3}, {4.4, 2.2}, {2.2, 2.2}}});

    REQUIRE(geom::envelope(geom) == geom::box_t{1.1, 1.1, 4.4, 3.3});
}
