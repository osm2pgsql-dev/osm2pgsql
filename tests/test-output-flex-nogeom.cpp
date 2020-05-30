#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("updating table without geometry should work")
{
    testing::opt_t options = testing::opt_t()
                                 .slim()
                                 .flex("test_output_flex_nogeom.lua")
                                 .srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV Tamenity=restaurant x10.0 y10.0\n"
                                  "n11 v1 dV Tamenity=post_box x10.0 y10.2\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_pois"));

    REQUIRE_NOTHROW(db.run_import(
        options.append(),
        "n10 v2 dV Tamenity=restaurant,name=Schwanen x10.0 y10.0\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_pois"));
}

