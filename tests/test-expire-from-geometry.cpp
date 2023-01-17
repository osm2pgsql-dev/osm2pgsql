/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <random>
#include <set>

#include "expire-tiles.hpp"
#include "reprojection.hpp"
#include "tile-output.hpp"
#include "tile.hpp"

static std::shared_ptr<reprojection> defproj{
    reprojection::create_projection(PROJ_SPHERE_MERC)};

// We are using zoom level 12 here, because at that level a tile is about
// 10,000 units wide/high which gives us easy numbers to work with.
static constexpr uint32_t const zoom = 12;

TEST_CASE("expire null geometry does nothing", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    SECTION("geom")
    {
        geom::geometry_t const geom{};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    REQUIRE(et.get_tiles().empty());
}

TEST_CASE("expire point at tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::point_t const pt{0.0, 0.0};

    SECTION("point") { et.from_geometry(pt, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{pt};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{pt};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 4);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[3], zoom) == tile_t{zoom, 2048, 2048});
}

TEST_CASE("expire point away from tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::point_t const pt{5000.0, 5000.0};

    SECTION("point") { et.from_geometry(pt, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{pt};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{pt};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2047});
}

TEST_CASE("expire linestring away from tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    SECTION("line")
    {
        geom::linestring_t const line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        et.from_geometry(line, expire_config);
    }

    SECTION("geom")
    {
        geom::linestring_t line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        geom::geometry_t const geom{std::move(line)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::linestring_t line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        geom::geometry_t geom{std::move(line)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2047});
}

TEST_CASE("expire linestring crossing tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    SECTION("line")
    {
        geom::linestring_t const line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        et.from_geometry(line, expire_config);
    }

    SECTION("geom")
    {
        geom::linestring_t line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        geom::geometry_t const geom{std::move(line)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::linestring_t line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        geom::geometry_t geom{std::move(line)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 2);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2048, 2047});
}

TEST_CASE("expire small polygon", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {5100.0, 5000.0},
                                    {5100.0, 5100.0},
                                    {5000.0, 5100.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {5100.0, 5000.0},
                              {5100.0, 5100.0},
                              {5000.0, 5100.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {5100.0, 5000.0},
                              {5100.0, 5100.0},
                              {5000.0, 5100.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2047});
}

TEST_CASE("expire large polygon as bbox", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.max_bbox = 40000;
    expire_tiles et{zoom, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 9);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2045});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2049, 2045});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2050, 2045});

    CHECK(tile_t::from_quadkey(tiles[3], zoom) == tile_t{zoom, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[4], zoom) == tile_t{zoom, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[7], zoom) == tile_t{zoom, 2050, 2046});

    CHECK(tile_t::from_quadkey(tiles[5], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[6], zoom) == tile_t{zoom, 2049, 2047});
    CHECK(tile_t::from_quadkey(tiles[8], zoom) == tile_t{zoom, 2050, 2047});
}

TEST_CASE("expire large polygon as boundary", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.max_bbox = 10000;
    expire_tiles et{zoom, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("polygon boundary")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_polygon_boundary(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 8);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2048, 2045});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2049, 2045});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2050, 2045});

    CHECK(tile_t::from_quadkey(tiles[3], zoom) == tile_t{zoom, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[6], zoom) == tile_t{zoom, 2050, 2046});

    CHECK(tile_t::from_quadkey(tiles[4], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[5], zoom) == tile_t{zoom, 2049, 2047});
    CHECK(tile_t::from_quadkey(tiles[7], zoom) == tile_t{zoom, 2050, 2047});
}

TEST_CASE("expire multipoint geometry", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::point_t p1{0.0, 0.0};
    geom::point_t p2{15000.0, 15000.0};

    SECTION("multipoint")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(std::move(p1));
        mpt.add_geometry(std::move(p2));
        et.from_geometry(mpt, expire_config);
    }

    SECTION("geom")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(std::move(p1));
        mpt.add_geometry(std::move(p2));
        geom::geometry_t const geom{std::move(mpt)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(std::move(p1));
        mpt.add_geometry(std::move(p2));
        geom::geometry_t geom{std::move(mpt)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 5);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[3], zoom) == tile_t{zoom, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[4], zoom) == tile_t{zoom, 2048, 2048});
}

TEST_CASE("expire multilinestring geometry", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::linestring_t l1{{2000.0, 2000.0}, {3000.0, 3000.0}};
    geom::linestring_t l2{{15000.0, 15000.0}, {25000.0, 15000.0}};
    geom::multilinestring_t ml;
    ml.add_geometry(std::move(l1));
    ml.add_geometry(std::move(l2));

    SECTION("multilinestring") { et.from_geometry(ml, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{std::move(ml)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{std::move(ml)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 3);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2050, 2046});
}

TEST_CASE("expire multipolygon geometry", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.max_bbox = 10000;
    expire_tiles et{zoom, defproj};

    geom::polygon_t p1{{{2000.0, 2000.0},
                        {2000.0, 3000.0},
                        {3000.0, 3000.0},
                        {3000.0, 2000.0},
                        {2000.0, 2000.0}}};

    geom::polygon_t p2{{{15000.0, 15000.0},
                        {45000.0, 15000.0},
                        {45000.0, 45000.0},
                        {15000.0, 45000.0},
                        {15000.0, 15000.0}}};
    p2.add_inner_ring({{25000.0, 25000.0},
                       {25000.0, 35000.0},
                       {35000.0, 35000.0},
                       {35000.0, 25000.0},
                       {25000.0, 25000.0}});

    geom::multipolygon_t mp;
    mp.add_geometry(std::move(p1));
    mp.add_geometry(std::move(p2));

    SECTION("multilinestring") { et.from_geometry(mp, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{std::move(mp)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{std::move(mp)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 17);

    std::set<quadkey_t> result;
    for (auto const &tile : tiles) {
        result.insert(tile);
    }

    std::set<quadkey_t> expected;
    expected.insert(tile_t{zoom, 2048, 2047}.quadkey()); // p1

    for (uint32_t x = 2049; x <= 2052; ++x) {
        for (uint32_t y = 2043; y <= 2046; ++y) {
            expected.insert(tile_t{zoom, x, y}.quadkey()); // p2
        }
    }
    REQUIRE(expected == result);
}

TEST_CASE("expire geometry collection", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::collection_t collection;
    collection.add_geometry(geom::geometry_t{geom::point_t{0.0, 0.0}});
    collection.add_geometry(geom::geometry_t{
        geom::linestring_t{{15000.0, 15000.0}, {25000.0, 15000.0}}});

    SECTION("geom")
    {
        geom::geometry_t const geom{std::move(collection)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{std::move(collection)};
        geom.set_srid(3857);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 6);
    CHECK(tile_t::from_quadkey(tiles[0], zoom) == tile_t{zoom, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], zoom) == tile_t{zoom, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[2], zoom) == tile_t{zoom, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[3], zoom) == tile_t{zoom, 2050, 2046});
    CHECK(tile_t::from_quadkey(tiles[4], zoom) == tile_t{zoom, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[5], zoom) == tile_t{zoom, 2048, 2048});
}

TEST_CASE("expire doesn't do anything if not in 3857", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles et{zoom, defproj};

    geom::geometry_t geom{geom::point_t{0.0, 0.0}};
    geom.set_srid(1234);
    et.from_geometry_if_3857(geom, expire_config);

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.empty());
}
