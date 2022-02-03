/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <vector>

#include "common-import.hpp"
#include "reprojection.hpp"

static testing::db::import_t db;

TEST_CASE("Projection setup")
{
    char const* const style_file = OSM2PGSQLDATA_DIR "default.style";

    std::vector<char const *> option_params = {"osm2pgsql", "-S", style_file,
                                               "--number-processes", "1"};

    std::string proj_name;
    char const *srid = "";

    SECTION("No options")
    {
        proj_name = "Spherical Mercator";
        srid = "3857";
    }

    SECTION("Latlong option")
    {
        option_params.push_back("-l");
        proj_name = "Latlong";
        srid = "4326";
    }

    SECTION("Mercator option")
    {
        option_params.push_back("-m");
        proj_name = "Spherical Mercator";
        srid = "3857";
    }

#ifdef HAVE_GENERIC_PROJ
    SECTION("Latlong with -E option")
    {
        proj_name = "Latlong";
        srid = "4326";
        option_params.push_back("-E");
        option_params.push_back(srid);
    }

    SECTION("Mercator with -E option")
    {
        proj_name = "Spherical Mercator";
        srid = "3857";
        option_params.push_back("-E");
        option_params.push_back(srid);
    }

    SECTION("Arbitrary projection with -E option")
    {
        srid = "32632";
        option_params.push_back("-E");
        option_params.push_back(srid);
    }
#endif

    option_params.push_back("foo");

    options_t options{(int)option_params.size(), (char **)option_params.data()};

    if (!proj_name.empty()) {
        CHECK(options.projection->target_desc() == proj_name);
    }

    db.run_import(options, "n1 Tamenity=bar x0 y0");

    auto conn = db.connect();

    CHECK(conn.result_as_string(
              "SELECT Find_SRID('public', 'planet_osm_roads', 'way')") == srid);
}
