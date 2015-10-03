/*

Test the area reprojection functionality of osm2pgsql 

The idea behind that functionality is to populate the way_area
column with the area that a polygoun would have in EPSG:3857, 
rather than the area it actually has in the coordinate system
used for importing.

This goes with a test data file named area-reprojection.osm

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
#include "parse.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/common-pg.hpp"

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


void test_area_base(bool latlon, bool reproj, double expect_area) {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        exit(77);
    }

    std::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
    options_t options;
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.style = "default.style";
    options.prefix = "osm2pgsql_test";
    if (latlon) {
        options.projection.reset(new reprojection(PROJ_LATLONG));
    }
    if (reproj) {
        options.reproject_area = true;
    }
    options.scale = latlon ? 10000000 : 100;

    auto out_test = std::make_shared<output_pgsql_t>(mid_pgsql.get(), options);

    osmdata_t osmdata(mid_pgsql, out_test);

    std::unique_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, false));

    osmdata.start();

    parser->stream_file("libxml2", "tests/area-reprojection.osm", &osmdata);

    parser.reset(nullptr);

    osmdata.stop();

    // tables should not contain any tag columns
    db->check_count(1, "select count(*) from osm2pgsql_test_polygon");
    db->check_number(expect_area, "SELECT way_area FROM osm2pgsql_test_polygon");

    return;
}

void test_area_classic() {
    test_area_base(false, false, 6.66e+10);
}

void test_area_latlon() {
    test_area_base(true, false, 6.66e-1);
}

void test_area_latlon_with_reprojection() {
    test_area_base(true, true, 6.66e+10);
}

int main(int argc, char *argv[]) {
    RUN_TEST(test_area_latlon);
    RUN_TEST(test_area_classic);
    RUN_TEST(test_area_latlon_with_reprojection);
    return 0;
}
