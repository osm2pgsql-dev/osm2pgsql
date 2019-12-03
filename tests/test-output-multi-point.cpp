#include <catch.hpp>

#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "parse-osmium.hpp"
#include "taginfo-impl.hpp"

#include "common-import.hpp"
#include "common-options.hpp"

TEST_CASE("parse point")
{
    testing::db::import_t db;

    options_t const options = testing::opt_t().slim();

    auto const processor = geometry_processor::create("point", &options);

    db.run_file_multi_output(testing::opt_t().slim(), processor,
                             "foobar_amenities", osmium::item_type::node,
                             "amenity", "liechtenstein-2013-08-03.osm.pbf");

    auto const conn = db.db().connect();
    conn.require_has_table("foobar_amenities");

    REQUIRE(244 == conn.get_count("foobar_amenities"));
    REQUIRE(36 == conn.get_count("foobar_amenities", "amenity='parking'"));
    REQUIRE(34 == conn.get_count("foobar_amenities", "amenity='bench'"));
    REQUIRE(1 ==
            conn.get_count("foobar_amenities", "amenity='vending_machine'"));
}
