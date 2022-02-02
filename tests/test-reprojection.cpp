/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "reprojection.hpp"

TEST_CASE("projection 4326", "[NoDB]")
{
    osmium::Location const loc{10.0, 53.0};
    int const srs = 4326;

    auto const reprojection = reprojection::create_projection(srs);
    REQUIRE(reprojection->target_srs() == srs);
    REQUIRE(reprojection->target_latlon());

    auto const c = reprojection->reproject(geom::point_t{loc});
    REQUIRE(c.x() == Approx(10.0));
    REQUIRE(c.y() == Approx(53.0));

    auto const ct = reprojection->target_to_tile(c);
    REQUIRE(ct.x() == Approx(1113194.91));
    REQUIRE(ct.y() == Approx(6982997.92));
}

TEST_CASE("projection 3857", "[NoDB]")
{
    osmium::Location const loc{10.0, 53.0};
    int const srs = 3857;

    auto const reprojection = reprojection::create_projection(srs);
    REQUIRE(reprojection->target_srs() == srs);
    REQUIRE_FALSE(reprojection->target_latlon());

    auto const c = reprojection->reproject(geom::point_t{loc});
    REQUIRE(c.x() == Approx(1113194.91));
    REQUIRE(c.y() == Approx(6982997.92));

    auto const ct = reprojection->target_to_tile(c);
    REQUIRE(ct.x() == Approx(1113194.91));
    REQUIRE(ct.y() == Approx(6982997.92));
}

#ifdef HAVE_GENERIC_PROJ
TEST_CASE("projection 5651", "[NoDB]")
{
    osmium::Location const loc{10.0, 53.0};
    int const srs = 5651; // ETRS89 / UTM zone 31N (N-zE)

    auto const reprojection = reprojection::create_projection(srs);
    REQUIRE(reprojection->target_srs() == srs);
    REQUIRE_FALSE(reprojection->target_latlon());

    auto const c = reprojection->reproject(geom::point_t{loc});
    REQUIRE(c.x() == Approx(31969448.78));
    REQUIRE(c.y() == Approx(5895222.39));

    auto const ct = reprojection->target_to_tile(c);
    REQUIRE(ct.x() == Approx(1113194.91));
    REQUIRE(ct.y() == Approx(6982997.92));
}
#endif
