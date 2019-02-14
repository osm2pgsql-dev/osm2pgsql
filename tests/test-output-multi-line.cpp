#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "osmtypes.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "taginfo_impl.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"
#include "tests/common.hpp"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        options_t options;
        options.database_options = db->database_options;
        options.num_procs = 1;
        options.slim = true;

        std::shared_ptr<geometry_processor> processor =
            geometry_processor::create("line", &options);

        export_list columns;
        {
            taginfo info;
            info.name = "highway";
            info.type = "text";
            columns.add(osmium::item_type::way, info);
        }

        std::shared_ptr<middle_t> mid_pgsql(new middle_pgsql_t(&options));
        mid_pgsql->start();
        auto midq = mid_pgsql->get_query_instance(mid_pgsql);
        // This actually uses the multi-backend with C transforms, not Lua transforms. This is unusual and doesn't reflect real practice
        auto out_test = std::make_shared<output_multi_t>(
            "foobar_highways", processor, columns, midq, options,
            std::make_shared<db_copy_thread_t>(
                options.database_options.conninfo()));

        osmdata_t osmdata(mid_pgsql, out_test);

        testing::parse("tests/liechtenstein-2013-08-03.osm.pbf", "pbf",
                       options, &osmdata);

        // start a new connection to run tests on
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'foobar_highways'");
        db->check_count(2753, "select count(*) from foobar_highways");

        //check that we have the right spread
        db->check_count(13, "select count(*) from foobar_highways where highway='bridleway'");
        db->check_count(3, "select count(*) from foobar_highways where highway='construction'");
        db->check_count(96, "select count(*) from foobar_highways where highway='cycleway'");
        db->check_count(249, "select count(*) from foobar_highways where highway='footway'");
        db->check_count(18, "select count(*) from foobar_highways where highway='living_street'");
        db->check_count(171, "select count(*) from foobar_highways where highway='path'");
        db->check_count(6, "select count(*) from foobar_highways where highway='pedestrian'");
        db->check_count(81, "select count(*) from foobar_highways where highway='primary'");
        db->check_count(842, "select count(*) from foobar_highways where highway='residential'");
        db->check_count(3, "select count(*) from foobar_highways where highway='road'");
        db->check_count(90, "select count(*) from foobar_highways where highway='secondary'");
        db->check_count(1, "select count(*) from foobar_highways where highway='secondary_link'");
        db->check_count(352, "select count(*) from foobar_highways where highway='service'");
        db->check_count(34, "select count(*) from foobar_highways where highway='steps'");
        db->check_count(33, "select count(*) from foobar_highways where highway='tertiary'");
        db->check_count(597, "select count(*) from foobar_highways where highway='track'");
        db->check_count(164, "select count(*) from foobar_highways where highway='unclassified'");
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
