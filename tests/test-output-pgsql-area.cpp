/**

Test the area reprojection functionality of osm2pgsql 

The idea behind that functionality is to populate the way_area
column with the area that a polygoun would have in EPSG:3857, 
rather than the area it actually has in the coordinate system
used for importing.

*/
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
#include "middle.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "taginfo_impl.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/common-pg.hpp"
#include "tests/common.hpp"

void run_test(const char* test_name, void (*testfunc)()) {
    try {
        fprintf(stderr, "%s\n", test_name);
        testfunc();

    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "FAIL\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "PASS\n");
}
#define RUN_TEST(x) run_test(#x, &(x))


void test_area_base(bool latlon, bool reproj, double expect_area_poly, double expect_area_multi) {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        exit(77);
    }

    options_t options;
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.slim = true;
    options.style = "default.style";
    options.prefix = "osm2pgsql_test";
    if (latlon) {
        options.projection.reset(reprojection::create_projection(PROJ_LATLONG));
    }
    if (reproj) {
        options.reproject_area = true;
    }

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_area.osm", "xml");

    db->check_count(2, "SELECT COUNT(*) FROM osm2pgsql_test_polygon");
    db->check_number(expect_area_poly, "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='poly'");
    db->check_number(expect_area_multi, "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='multi'");

    return;
}

void test_area_classic() {
    test_area_base(false, false, 1.23927e+10, 9.91828e+10);
}

void test_area_latlon() {
    test_area_base(true, false, 1, 8);
}

void test_area_latlon_with_reprojection() {
    test_area_base(true, true, 1.23927e+10, 9.91828e+10);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    RUN_TEST(test_area_latlon);
    RUN_TEST(test_area_classic);
    RUN_TEST(test_area_latlon_with_reprojection);
    return 0;
}
