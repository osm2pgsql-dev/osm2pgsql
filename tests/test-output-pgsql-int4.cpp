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
void test_int4() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql-int4"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.slim = 1;
    options.prefix = "osm2pgsql_test";
    options.style = "tests/test_output_pgsql_int4.style";

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_int4.osm",
                           "xml");

    db->assert_has_table("osm2pgsql_test_point");
    db->assert_has_table("osm2pgsql_test_line");
    db->assert_has_table("osm2pgsql_test_polygon");
    db->assert_has_table("osm2pgsql_test_roads");

    // First three nodes have population values that are out of range for int4 columns
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 1");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 2");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 3");
    // Check values that are valid for int4 columns, including limits
    db->check_count(2147483647, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 4");
    db->check_count(10000, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 5");
    db->check_count(-10000, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 6");
    db->check_count(-2147483648, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 7");
    // More out of range negative values
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 8");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 9");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 10");

    // Ranges are also parsed into int4 columns
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 11");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 12");
    // Check values that are valid for int4 columns, including limits
    db->check_count(2147483647, "SELECT population FROM osm2pgsql_test_point WHERE osm_id =13");
    db->check_count(15000, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 14");
    db->check_count(-15000, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 15");
    db->check_count(-2147483648, "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 16");
    // More out of range negative values
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 17");
    db->check_string("", "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 18");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    RUN_TEST(test_int4);

    return 0;
}
