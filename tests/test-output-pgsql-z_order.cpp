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
#include "output-pgsql.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "taginfo_impl.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"
#include "tests/common.hpp"

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
void test_z_order() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql-z_order"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.slim = 1;
    options.prefix = "osm2pgsql_test";
    options.style = "default.style";

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_z_order.osm",
                           "xml");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    db->check_string("motorway", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 0");
    db->check_string("trunk", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 1");
    db->check_string("primary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 2");
    db->check_string("secondary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 3");
    db->check_string("tertiary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 4");

    db->check_string("residential", "SELECT highway FROM osm2pgsql_test_line ORDER BY z_order DESC LIMIT 1 OFFSET 0");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    RUN_TEST(test_z_order);

    return 0;
}
