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
    options.database_options = db.db().db_options();

    export_list columns;
    {
        taginfo info;
        info.name = "amenity";
        info.type = "text";
        columns.add(osmium::item_type::node, info);
    }

    auto mid_pgsql = std::make_shared<middle_pgsql_t>(&options);
    mid_pgsql->start();
    auto const midq = mid_pgsql->get_query_instance();

    std::vector<std::shared_ptr<output_t>> outputs;

    // let's make lots of tables!
    for (int i = 0; i < 10; ++i) {
        std::string const name{"foobar_{}"_format(i)};

        auto processor = geometry_processor::create("point", &options);

        auto out_test = std::make_shared<output_multi_t>(
            name, processor, columns, midq, options,
            std::make_shared<db_copy_thread_t>(
                options.database_options.conninfo()));

        outputs.push_back(out_test);
    }

    auto dependency_manager =
        std::unique_ptr<dependency_manager_t>(new dependency_manager_t{});

    testing::parse_file(options, std::move(dependency_manager), mid_pgsql,
                        outputs, "liechtenstein-2013-08-03.osm.pbf");

    auto conn = db.db().connect();

    for (int i = 0; i < 10; ++i) {
        std::string const buf{"foobar_{}"_format(i)};
        char const *name = buf.c_str();

        conn.require_has_table(name);

        REQUIRE(244 == conn.get_count(name));
        REQUIRE(36 == conn.get_count(name, "amenity='parking'"));
        REQUIRE(34 == conn.get_count(name, "amenity='bench'"));
        REQUIRE(1 == conn.get_count(name, "amenity='vending_machine'"));
    }
}
