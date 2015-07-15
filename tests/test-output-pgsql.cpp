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
#include "parse.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

namespace {

struct skip_test : public std::exception {
    const char *what() { return "Test skipped."; }
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

void check_count(pg::conn_ptr &conn, int expected, const std::string &query) {
    pg::result_ptr res = conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query "
                                                "to check COUNT(*), but got %1%. Query "
                                                "was: %2%.")
                                  % ntuples % query).str());
    }

    std::string numstr = PQgetvalue(res->get(), 0, 0);
    int count = boost::lexical_cast<int>(numstr);

    if (count != expected) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % count % query).str());
    }
}

void check_number(pg::conn_ptr &conn, double expected, const std::string &query) {
    pg::result_ptr res = conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query, "
                                                " but got %1%. Query was: %2%.")
                                  % ntuples % query).str());
    }

    std::string numstr = PQgetvalue(res->get(), 0, 0);
    double num = boost::lexical_cast<double>(numstr);

    // floating point isn't exact, so allow a 0.01% difference
    if ((num > 1.0001*expected) || (num < 0.9999*expected)) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % num % query).str());
    }
}

void assert_has_table(pg::conn_ptr &test_conn, const std::string &table_name) {
    std::string query = (boost::format("select count(*) from pg_catalog.pg_class "
                                       "where relname = '%1%'")
                         % table_name).str();

    check_count(test_conn, 1, query);
}

// "simple" test modeled on the basic regression test from
// the python script. this is just to check everything is
// working as expected before we start the complex stuff.
void test_regression_simple() {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], NULL };

    boost::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
    options_t options = options_t::parse(2, argv);
    options.conninfo = db->conninfo().c_str();
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    boost::shared_ptr<output_pgsql_t> out_test(new output_pgsql_t(mid_pgsql.get(), options));

    osmdata_t osmdata(mid_pgsql, out_test);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

    osmdata.start();

    parser->stream_file("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_count(test_conn, 1342, "SELECT count(*) FROM osm2pgsql_test_point");
    check_count(test_conn, 3300, "SELECT count(*) FROM osm2pgsql_test_line");
    check_count(test_conn,  375, "SELECT count(*) FROM osm2pgsql_test_roads");
    check_count(test_conn, 4128, "SELECT count(*) FROM osm2pgsql_test_polygon");

    // Check size of lines
    check_number(test_conn, 1696.04, "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 44822682");
    check_number(test_conn, 1151.26, "SELECT ST_Length(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_line WHERE osm_id = 44822682");

    check_number(test_conn, 311.21, "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");
    check_number(test_conn, 311.21, "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");
    check_number(test_conn, 143.81, "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");

    // Check a point's location
    check_count(test_conn, 1, "SELECT count(*) FROM osm2pgsql_test_point WHERE ST_DWithin(way, 'SRID=900913;POINT(1062645.12 5972593.4)'::geometry, 0.1)");
}

void test_latlong() {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], NULL };

    boost::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
    options_t options = options_t::parse(2, argv);
    options.conninfo = db->conninfo().c_str();
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    options.projection.reset(new reprojection(PROJ_LATLONG));
    options.scale = (options.projection->get_proj_id() == PROJ_LATLONG) ? 10000000 : 100;

    boost::shared_ptr<output_pgsql_t> out_test(new output_pgsql_t(mid_pgsql.get(), options));

    osmdata_t osmdata(mid_pgsql, out_test);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

    osmdata.start();

    parser->stream_file("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_count(test_conn, 1342, "SELECT count(*) FROM osm2pgsql_test_point");
    check_count(test_conn, 3298, "SELECT count(*) FROM osm2pgsql_test_line");
    check_count(test_conn, 374, "SELECT count(*) FROM osm2pgsql_test_roads");
    check_count(test_conn, 4128, "SELECT count(*) FROM osm2pgsql_test_polygon");

    // Check size of lines
    check_number(test_conn, 0.0105343, "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 44822682");
    check_number(test_conn, 1151.26, "SELECT ST_Length(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_line WHERE osm_id = 44822682");

    check_number(test_conn, 1.70718e-08, "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");
    check_number(test_conn, 1.70718e-08, "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");
    check_number(test_conn, 143.845, "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM osm2pgsql_test_polygon WHERE osm_id = 157261342");

    // Check a point's location
    check_count(test_conn, 1, "SELECT count(*) FROM osm2pgsql_test_point WHERE ST_DWithin(way, 'SRID=4326;POINT(9.5459035 47.1866494)'::geometry, 0.00001)");
}


void test_area_way_simple() {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], NULL };

    boost::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
    options_t options = options_t::parse(2, argv);
    options.conninfo = db->conninfo().c_str();
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";
    options.flat_node_cache_enabled = true;
    options.flat_node_file = boost::optional<std::string>("tests/test_output_pgsql_area_way.flat.nodes.bin");

    boost::shared_ptr<output_pgsql_t> out_test(new output_pgsql_t(mid_pgsql.get(), options));

    osmdata_t osmdata(mid_pgsql, out_test);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

    osmdata.start();

    parser->stream_file("libxml2", "tests/test_output_pgsql_way_area.osm", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_count(test_conn, 0, "SELECT count(*) FROM osm2pgsql_test_point");
    check_count(test_conn, 0, "SELECT count(*) FROM osm2pgsql_test_line");
    check_count(test_conn, 0, "SELECT count(*) FROM osm2pgsql_test_roads");
    check_count(test_conn, 1, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

void test_route_rel() {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], NULL };

    boost::shared_ptr<middle_ram_t> mid_ram(new middle_ram_t());
    options_t options = options_t::parse(2, argv);
    options.conninfo = db->conninfo().c_str();
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = false;
    options.style = "default.style";

    boost::shared_ptr<output_pgsql_t> out_test(new output_pgsql_t(mid_ram.get(), options));

    osmdata_t osmdata(mid_ram, out_test);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

    osmdata.start();

    parser->stream_file("libxml2", "tests/test_output_pgsql_route_rel.osm", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_count(test_conn, 0, "SELECT count(*) FROM osm2pgsql_test_point");
    check_count(test_conn, 2, "SELECT count(*) FROM osm2pgsql_test_line");
    check_count(test_conn, 1, "SELECT count(*) FROM osm2pgsql_test_roads");
    check_count(test_conn, 0, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

// test the same, but clone the output. it should
// behave the same as the original.
void test_clone() {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        throw skip_test();
    }

    std::string proc_name("test-output-pgsql"), input_file("-");
    char *argv[] = { &proc_name[0], &input_file[0], NULL };

    boost::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
    options_t options = options_t::parse(2, argv);
    options.conninfo = db->conninfo().c_str();
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;
    options.style = "default.style";

    output_pgsql_t out_test(mid_pgsql.get(), options);

    //TODO: make the middle testable too
    //boost::shared_ptr<middle_t> mid_clone = mid_pgsql->get_instance();
    boost::shared_ptr<output_t> out_clone = out_test.clone(mid_pgsql.get());

    osmdata_t osmdata(mid_pgsql, out_clone);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

    osmdata.start();

    parser->stream_file("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_count(test_conn, 1342, "SELECT count(*) FROM osm2pgsql_test_point");
    check_count(test_conn, 3300, "SELECT count(*) FROM osm2pgsql_test_line");
    check_count(test_conn,  375, "SELECT count(*) FROM osm2pgsql_test_roads");
    check_count(test_conn, 4128, "SELECT count(*) FROM osm2pgsql_test_polygon");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    RUN_TEST(test_regression_simple);
    RUN_TEST(test_latlong);
    RUN_TEST(test_clone);
    RUN_TEST(test_area_way_simple);
    RUN_TEST(test_route_rel);

    return 0;
}
