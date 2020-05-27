#include <catch.hpp>

#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "taginfo-impl.hpp"

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("parse point")
{
    options_t options = testing::opt_t().slim();

    auto processor = geometry_processor::create("polygon", &options);

    db.run_file_multi_output(testing::opt_t().slim(), processor,
                             "foobar_buildings", osmium::item_type::way,
                             "building", "liechtenstein-2013-08-03.osm.pbf");

    auto conn = db.db().connect();
    conn.require_has_table("foobar_buildings");

    REQUIRE(3723 == conn.get_count("foobar_buildings"));
    REQUIRE(0 == conn.get_count("foobar_buildings", "building is null"));

    REQUIRE(1 == conn.get_count("foobar_buildings", "building='barn'"));
    REQUIRE(1 == conn.get_count("foobar_buildings", "building='chapel'"));
    REQUIRE(5 == conn.get_count("foobar_buildings", "building='church'"));
    REQUIRE(3 == conn.get_count("foobar_buildings", "building='commercial'"));
    REQUIRE(6 == conn.get_count("foobar_buildings", "building='farm'"));
    REQUIRE(1 == conn.get_count("foobar_buildings", "building='garage'"));
    REQUIRE(2 == conn.get_count("foobar_buildings", "building='glasshouse'"));
    REQUIRE(1 == conn.get_count("foobar_buildings", "building='greenhouse'"));
    REQUIRE(153 == conn.get_count("foobar_buildings", "building='house'"));
    REQUIRE(4 == conn.get_count("foobar_buildings", "building='hut'"));
    REQUIRE(8 == conn.get_count("foobar_buildings", "building='industrial'"));
    REQUIRE(200 ==
            conn.get_count("foobar_buildings", "building='residential'"));
    REQUIRE(6 == conn.get_count("foobar_buildings", "building='roof'"));
    REQUIRE(4 == conn.get_count("foobar_buildings", "building='school'"));
    REQUIRE(2 == conn.get_count("foobar_buildings", "building='station'"));
    REQUIRE(3 == conn.get_count("foobar_buildings", "building='warehouse'"));
    REQUIRE(3323 == conn.get_count("foobar_buildings", "building='yes'"));
}
