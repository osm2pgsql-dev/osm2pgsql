#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "db-copy.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "taginfo_impl.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/common-pg.hpp"
#include "tests/common-cleanup.hpp"
#include "tests/common.hpp"

#define FLAT_NODES_FILE_NAME "tests/test_output_pgsql_area_way.flat.nodes.bin"

namespace {

struct skip_test : public std::exception {
    const char *what() const noexcept { return "Test skipped."; }
};

void run_test(const char* test_name, void (*testfunc)()) {
    try {
        fprintf(stderr, "%s\n", test_name);
        testfunc();

    } catch (const skip_test &) {
        exit(77); // <-- code to skip this test.

    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "FAIL\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "PASS\n");
}
#define RUN_TEST(x) run_test(#x, &(x))

// "simple" test modeled on the basic regression test from
// the python script. this is just to check everything is
// working as expected before we start the complex stuff.
void test_regression_simple() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    testing::run_osm2pgsql(options, "tests/liechtenstein-2013-08-03.osm.pbf",
                           "pbf");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_count(1342, "SELECT count(*) FROM osm2pgsql_test_point");
    db->check_count(3231, "SELECT count(*) FROM osm2pgsql_test_line");
    db->check_count( 375, "SELECT count(*) FROM osm2pgsql_test_roads");
    db->check_count(4130, "SELECT count(*) FROM osm2pgsql_test_polygon");

    // Check size of lines
    db->check_number(1696.04, "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    db->check_number(1151.26, "SELECT ST_Length(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_line WHERE osm_id = 1101");

    db->check_number(311.289, "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    db->check_number(311.289, "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    db->check_number(143.845, "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    db->check_count(1, "SELECT count(*) FROM osm2pgsql_test_point WHERE ST_DWithin(way, 'SRID=3857;POINT(1062645.12 5972593.4)'::geometry, 0.1)");
}

void test_latlong() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    options.projection.reset(reprojection::create_projection(PROJ_LATLONG));

    testing::run_osm2pgsql(options, "tests/liechtenstein-2013-08-03.osm.pbf",
                           "pbf");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_count(1342, "SELECT count(*) FROM osm2pgsql_test_point");
    db->check_count(3229, "SELECT count(*) FROM osm2pgsql_test_line");
    db->check_count(374, "SELECT count(*) FROM osm2pgsql_test_roads");
    db->check_count(4130, "SELECT count(*) FROM osm2pgsql_test_polygon");

    // Check size of lines
    db->check_number(0.0105343, "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    db->check_number(1151.26, "SELECT ST_Length(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_line WHERE osm_id = 1101");

    db->check_number(1.70718e-08, "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    db->check_number(1.70718e-08, "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    db->check_number(143.845, "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    db->check_count(1, "SELECT count(*) FROM osm2pgsql_test_point WHERE ST_DWithin(way, 'SRID=4326;POINT(9.5459035 47.1866494)'::geometry, 0.00001)");
}


void test_area_way_simple() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";
    options.flat_node_cache_enabled = true;
    options.flat_node_file = boost::optional<std::string>(FLAT_NODES_FILE_NAME);

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_way_area.osm",
                           "xml");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_count(0, "SELECT count(*) FROM osm2pgsql_test_point");
    db->check_count(0, "SELECT count(*) FROM osm2pgsql_test_line");
    db->check_count(0, "SELECT count(*) FROM osm2pgsql_test_roads");
    db->check_count(1, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

void test_route_rel() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = false;
    options.style = "default.style";

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_route_rel.osm",
                           "xml");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_count(0, "SELECT count(*) FROM osm2pgsql_test_point");
    db->check_count(2, "SELECT count(*) FROM osm2pgsql_test_line");
    db->check_count(1, "SELECT count(*) FROM osm2pgsql_test_roads");
    db->check_count(0, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

// test the same, but clone the output. it should
// behave the same as the original.
void test_clone() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    auto mid_pgsql = std::make_shared<middle_pgsql_t>(&options);
    mid_pgsql->start();
    auto ct =
        std::make_shared<db_copy_thread_t>(db->database_options.conninfo());
    output_pgsql_t out_test(mid_pgsql->get_query_instance(mid_pgsql), options,
                            ct);

    std::shared_ptr<output_t> out_clone =
        out_test.clone(mid_pgsql->get_query_instance(mid_pgsql), ct);

    osmdata_t osmdata(std::static_pointer_cast<middle_t>(mid_pgsql), out_clone);

    testing::parse("tests/liechtenstein-2013-08-03.osm.pbf", "pbf",
                   options, &osmdata);

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_count(1342, "SELECT count(*) FROM osm2pgsql_test_point");
    db->check_count(3231, "SELECT count(*) FROM osm2pgsql_test_line");
    db->check_count( 375, "SELECT count(*) FROM osm2pgsql_test_roads");
    db->check_count(4130, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // remove flat nodes file  on exit - it's 20GB and bad manners to
    // leave that lying around on the filesystem.
    cleanup::file flat_nodes_file(FLAT_NODES_FILE_NAME);

    RUN_TEST(test_regression_simple);
    RUN_TEST(test_latlong);
    RUN_TEST(test_clone);
    RUN_TEST(test_area_way_simple);
    RUN_TEST(test_route_rel);

    return 0;
}
