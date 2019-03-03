#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "middle-pgsql.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "output-multi.hpp"
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

        options.projection.reset(reprojection::create_projection(PROJ_LATLONG));

        options.output_backend = "multi";
        options.style = "tests/test_output_multi_tags.json";

        testing::run_osm2pgsql(options, "tests/test_output_multi_tags.osm",
                               "xml");

        // Check we got the right tables
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_points_1'");
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_points_2'");
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_line_1'");
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_polygon_1'");
        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_polygon_2'");

        // Check we didn't get any extra in the tables
        db->check_count(2, "select count(*) from test_points_1");
        db->check_count(2, "select count(*) from test_points_2");
        db->check_count(1, "select count(*) from test_line_1");
        db->check_count(1, "select count(*) from test_line_2");
        db->check_count(1, "select count(*) from test_polygon_1");
        db->check_count(1, "select count(*) from test_polygon_2");

        // Check that the first table for each type got the right transform
        db->check_count(1, "SELECT COUNT(*) FROM test_points_1 WHERE foo IS NULL and bar = 'n1' AND baz IS NULL");
        db->check_count(1, "SELECT COUNT(*) FROM test_points_1 WHERE foo IS NULL and bar = 'n2' AND baz IS NULL");
        db->check_count(1, "SELECT COUNT(*) FROM test_line_1 WHERE foo IS NULL and bar = 'w1' AND baz IS NULL");
        db->check_count(1, "SELECT COUNT(*) FROM test_polygon_1 WHERE foo IS NULL and bar = 'w2' AND baz IS NULL");

        // Check that the second table also got the right transform
        db->check_count(1, "SELECT COUNT(*) FROM test_points_2 WHERE foo IS NULL and bar IS NULL AND baz = 'n1'");
        db->check_count(1, "SELECT COUNT(*) FROM test_points_2 WHERE foo IS NULL and bar IS NULL AND baz = 'n2'");
        db->check_count(1, "SELECT COUNT(*) FROM test_line_2 WHERE foo IS NULL and bar IS NULL AND baz = 'w1'");
        db->check_count(1, "SELECT COUNT(*) FROM test_polygon_2 WHERE foo IS NULL and bar IS NULL AND baz = 'w2'");

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
