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

void test_other_output_schema() {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    pg::conn_ptr schema_conn = pg::conn::connect(db->database_options);

    schema_conn->exec("CREATE SCHEMA myschema;"
                      "CREATE TABLE myschema.osm2pgsql_test_point (id bigint);"
                      "CREATE TABLE myschema.osm2pgsql_test_line (id bigint);"
                      "CREATE TABLE myschema.osm2pgsql_test_polygon (id bigint);"
                      "CREATE TABLE myschema.osm2pgsql_test_roads (id bigint)");

    std::string proc_name("test-output-pgsql-schema"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], nullptr };

    options_t options = options_t(2, argv);
    options.database_options = db->database_options;
    options.num_procs = 1;
    options.slim = true;
    options.prefix = "osm2pgsql_test";
    options.style = "default.style";

    testing::run_osm2pgsql(options, "tests/test_output_pgsql_z_order.osm",
                           "xml");

    db->assert_has_table("public.osm2pgsql_test_point");
    db->assert_has_table("public.osm2pgsql_test_line");
    db->assert_has_table("public.osm2pgsql_test_polygon");
    db->assert_has_table("public.osm2pgsql_test_roads");
    db->assert_has_table("public.osm2pgsql_test_point");
    db->assert_has_table("public.osm2pgsql_test_line");
    db->assert_has_table("public.osm2pgsql_test_polygon");
    db->assert_has_table("public.osm2pgsql_test_roads");

    db->check_count( 2, "SELECT COUNT(*) FROM osm2pgsql_test_point");
    db->check_count( 11, "SELECT COUNT(*) FROM osm2pgsql_test_line");
    db->check_count( 1, "SELECT COUNT(*) FROM osm2pgsql_test_polygon");
    db->check_count( 8, "SELECT COUNT(*) FROM osm2pgsql_test_roads");
    db->check_count( 0, "SELECT COUNT(*) FROM myschema.osm2pgsql_test_point");
    db->check_count( 0, "SELECT COUNT(*) FROM myschema.osm2pgsql_test_line");
    db->check_count( 0, "SELECT COUNT(*) FROM myschema.osm2pgsql_test_polygon");
    db->check_count( 0, "SELECT COUNT(*) FROM myschema.osm2pgsql_test_roads");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    RUN_TEST(test_other_output_schema);

    return 0;
}
