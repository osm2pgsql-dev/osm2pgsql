/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "tile.hpp"

TEST_CASE("invalid tile", "[NoDB]")
{
    tile_t const tile;
    REQUIRE_FALSE(tile.valid());
}

TEST_CASE("tile access and comparison", "[NoDB]")
{
    tile_t const a{3, 2, 1};
    tile_t const b{3, 2, 1};
    tile_t const c{3, 1, 2};

    REQUIRE(a.valid());
    REQUIRE(b.valid());
    REQUIRE(c.valid());

    REQUIRE(a.zoom() == 3);
    REQUIRE(a.x() == 2);
    REQUIRE(a.y() == 1);

    REQUIRE(b.zoom() == 3);
    REQUIRE(b.x() == 2);
    REQUIRE(b.y() == 1);

    REQUIRE(c.zoom() == 3);
    REQUIRE(c.x() == 1);
    REQUIRE(c.y() == 2);

    REQUIRE(a == b);
    REQUIRE_FALSE(a != b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a != c);

    REQUIRE_FALSE(a < b);
    REQUIRE_FALSE(b < a);

    REQUIRE_FALSE(a < c);
    REQUIRE(c < a);
}

TEST_CASE("tile_t coordinates zoom=0", "[NoDB]")
{
    tile_t const tile{0, 0, 0};

    REQUIRE(tile.xmin() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.ymin() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.xmax() == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.ymax() == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.box().min_x() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.box().min_y() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.box().max_x() == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.box().max_y() == Approx(tile_t::half_earth_circumference));

    // Bounding box with margin will not get larger, because it is always
    // clamped to the full extent of the map.
    REQUIRE(tile.xmin(0.1) == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.ymin(0.1) == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.xmax(0.1) == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.ymax(0.1) == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.box(0.1).min_x() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.box(0.1).min_y() == Approx(-tile_t::half_earth_circumference));
    REQUIRE(tile.box(0.1).max_x() == Approx(tile_t::half_earth_circumference));
    REQUIRE(tile.box(0.1).max_y() == Approx(tile_t::half_earth_circumference));

    REQUIRE(tile.center().x() == Approx(0.0));
    REQUIRE(tile.center().y() == Approx(0.0));
    REQUIRE(tile.center() == geom::point_t{0.0, 0.0});

    REQUIRE(tile.extent() == Approx(tile_t::earth_circumference));

    geom::point_t const p{12345.6, 7891.0};
    auto const tp = tile.to_tile_coords(p, 256);
    auto const pp = tile.to_world_coords(tp, 256);
    REQUIRE(p.x() == Approx(pp.x()));
    REQUIRE(p.y() == Approx(pp.y()));

    REQUIRE(tile.quadkey() == quadkey_t{0});
}

TEST_CASE("tile_t coordinates zoom=2", "[NoDB]")
{
    tile_t const tile{2, 1, 2};

    double min = -tile_t::half_earth_circumference / 2;
    double max = 0.0;
    REQUIRE(tile.xmin() == Approx(min));
    REQUIRE(tile.ymin() == Approx(min));
    REQUIRE(tile.xmax() == Approx(max));
    REQUIRE(tile.ymax() == Approx(max));
    CHECK(tile.box().min_x() == tile.xmin());
    CHECK(tile.box().min_y() == tile.ymin());
    CHECK(tile.box().max_x() == tile.xmax());
    CHECK(tile.box().max_y() == tile.ymax());

    // Bounding box of tile with 50% margin on all sides.
    min -= tile_t::half_earth_circumference / 4;
    max += tile_t::half_earth_circumference / 4;
    CHECK(tile.xmin(0.5) == Approx(min));
    CHECK(tile.ymin(0.5) == Approx(min));
    CHECK(tile.xmax(0.5) == Approx(max));
    CHECK(tile.ymax(0.5) == Approx(max));
    CHECK(tile.box(0.5).min_x() == tile.xmin(0.5));
    CHECK(tile.box(0.5).min_y() == tile.ymin(0.5));
    CHECK(tile.box(0.5).max_x() == tile.xmax(0.5));
    CHECK(tile.box(0.5).max_y() == tile.ymax(0.5));

    REQUIRE(tile.center().x() == Approx(-tile_t::half_earth_circumference / 4));
    REQUIRE(tile.center().y() == Approx(-tile_t::half_earth_circumference / 4));

    REQUIRE(tile.extent() == Approx(tile_t::half_earth_circumference / 2));

    geom::point_t const p{-tile_t::half_earth_circumference / 4,
                          -tile_t::half_earth_circumference / 8};
    auto const tp = tile.to_tile_coords(p, 4096);
    REQUIRE(tp.x() == Approx(2048));
    REQUIRE(tp.y() == Approx(2048 + 1024));

    auto const pp = tile.to_world_coords(tp, 4096);
    REQUIRE(p.x() == Approx(pp.x()));
    REQUIRE(p.y() == Approx(pp.y()));

    auto const q = tile.quadkey();
    REQUIRE(tile == tile_t::from_quadkey(q, tile.zoom()));
}
