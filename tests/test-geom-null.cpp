/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom.hpp"

TEST_CASE("null geometry", "[NoDB]")
{
    geom::geometry_t const geom{};

    REQUIRE(num_geometries(geom) == 0);
    REQUIRE(area(geom) == 0.0);
    REQUIRE(geometry_type(geom) == "NULL");
    REQUIRE(centroid(geom).is_null());
}
