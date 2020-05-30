#include <catch.hpp>

#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "taginfo-impl.hpp"

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("parse linestring")
{
    options_t options = testing::opt_t().slim();

    auto processor = geometry_processor::create("line", &options);

    db.run_file_multi_output(testing::opt_t().slim(), processor,
                             "foobar_highways", osmium::item_type::way,
                             "highway", "liechtenstein-2013-08-03.osm.pbf");

    auto conn = db.db().connect();
    conn.require_has_table("foobar_highways");

    REQUIRE(2753 == conn.get_count("foobar_highways"));
    REQUIRE(13 == conn.get_count("foobar_highways", "highway='bridleway'"));
    REQUIRE(3 == conn.get_count("foobar_highways", "highway='construction'"));
    REQUIRE(96 == conn.get_count("foobar_highways", "highway='cycleway'"));
    REQUIRE(249 == conn.get_count("foobar_highways", "highway='footway'"));
    REQUIRE(18 == conn.get_count("foobar_highways", "highway='living_street'"));
    REQUIRE(171 == conn.get_count("foobar_highways", "highway='path'"));
    REQUIRE(6 == conn.get_count("foobar_highways", "highway='pedestrian'"));
    REQUIRE(81 == conn.get_count("foobar_highways", "highway='primary'"));
    REQUIRE(842 == conn.get_count("foobar_highways", "highway='residential'"));
    REQUIRE(3 == conn.get_count("foobar_highways", "highway='road'"));
    REQUIRE(90 == conn.get_count("foobar_highways", "highway='secondary'"));
    REQUIRE(1 == conn.get_count("foobar_highways", "highway='secondary_link'"));
    REQUIRE(352 == conn.get_count("foobar_highways", "highway='service'"));
    REQUIRE(34 == conn.get_count("foobar_highways", "highway='steps'"));
    REQUIRE(33 == conn.get_count("foobar_highways", "highway='tertiary'"));
    REQUIRE(597 == conn.get_count("foobar_highways", "highway='track'"));
    REQUIRE(164 == conn.get_count("foobar_highways", "highway='unclassified'"));
}
