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

void check_string(pg::conn_ptr &conn, std::string expected, const std::string &query) {
    pg::result_ptr res = conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query "
                                                "to check a string, but got %1%. Query "
                                                "was: %2%.")
                                  % ntuples % query).str());
    }

    std::string actual = PQgetvalue(res->get(), 0, 0);

    if (actual != expected) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % actual % query).str());
    }
}


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
void test_z_order() {
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
    options.style = "default.style";

    boost::shared_ptr<output_pgsql_t> out_test(new output_pgsql_t(mid_pgsql.get(), options));

    osmdata_t osmdata(mid_pgsql, out_test);

    boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, false));

    osmdata.start();

    parser->stream_file("libxml2", "tests/test_output_pgsql_z_order.osm", &osmdata);

    parser.reset(NULL);

    osmdata.stop();

    // start a new connection to run tests on
    pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

    assert_has_table(test_conn, "osm2pgsql_test_point");
    assert_has_table(test_conn, "osm2pgsql_test_line");
    assert_has_table(test_conn, "osm2pgsql_test_polygon");
    assert_has_table(test_conn, "osm2pgsql_test_roads");

    check_string(test_conn, "motorway", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 0");
    check_string(test_conn, "trunk", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 1");
    check_string(test_conn, "primary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 2");
    check_string(test_conn, "secondary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 3");
    check_string(test_conn, "tertiary", "SELECT highway FROM osm2pgsql_test_line WHERE layer IS NULL ORDER BY z_order DESC LIMIT 1 OFFSET 4");

    check_string(test_conn, "residential", "SELECT highway FROM osm2pgsql_test_line ORDER BY z_order DESC LIMIT 1 OFFSET 0");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    RUN_TEST(test_z_order);

    return 0;
}
