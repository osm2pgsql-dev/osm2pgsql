#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <stdexcept>

#include <sys/types.h>
#include <unistd.h>

#include <boost/format.hpp>

#include <options.hpp>

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

void expect_conninfo(const database_options_t &db, const std::string &expect) {
    if (db.conninfo() != expect) {
        throw std::runtime_error((boost::format("Expected connection info of %1%, got %2%.") % expect % db.conninfo()).str());
    }
}

/**
 * Tests that the conninfo strings are appropriately generated
 * This test is stricter than it needs to be, as it also cares about order,
 * but the current implementation always uses the same order, and attempting to
 * parse a conninfo string is complex.
 */
void test_conninfo() {
    database_options_t db;
    expect_conninfo(db, "fallback_application_name='osm2pgsql'");
    db.db = "foo";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' dbname='foo'");

    db = database_options_t();
    db.username = "bar";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' user='bar'");

    db = database_options_t();
    db.password = "bar";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' password='bar'");

    db = database_options_t();
    db.host = "bar";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' host='bar'");

    db = database_options_t();
    db.port = "bar";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' port='bar'");

    db = database_options_t();
    db.db = "foo";
    db.username = "bar";
    db.password = "baz";
    db.host = "bzz";
    db.port = "123";
    expect_conninfo(db, "fallback_application_name='osm2pgsql' dbname='foo' user='bar' password='baz' host='bzz' port='123'");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    RUN_TEST(test_conninfo);

    return 0;
}
