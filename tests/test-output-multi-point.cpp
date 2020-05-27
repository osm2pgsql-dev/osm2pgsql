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

    auto processor = geometry_processor::create("point", &options);

    db.run_file_multi_output(testing::opt_t().slim(), processor,
                             "foobar_amenities", osmium::item_type::node,
                             "amenity", "liechtenstein-2013-08-03.osm.pbf");

    auto conn = db.db().connect();
    conn.require_has_table("foobar_amenities");

    REQUIRE(244 == conn.get_count("foobar_amenities"));
    REQUIRE(36 == conn.get_count("foobar_amenities", "amenity='parking'"));
    REQUIRE(34 == conn.get_count("foobar_amenities", "amenity='bench'"));
    REQUIRE(1 ==
            conn.get_count("foobar_amenities", "amenity='vending_machine'"));
}
