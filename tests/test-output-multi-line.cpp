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
#include "output-multi.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "taginfo_impl.hpp"
#include "parse.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

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

int main(int argc, char *argv[]) {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        std::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
        options_t options;
        options.conninfo = db->conninfo().c_str();
        options.num_procs = 1;
        options.slim = true;

        std::shared_ptr<geometry_processor> processor =
            geometry_processor::create("line", &options);

        export_list columns;
        { taginfo info; info.name = "highway"; info.type = "text"; columns.add(OSMTYPE_WAY, info); }

        auto out_test = std::make_shared<output_multi_t>("foobar_highways", processor, columns, mid_pgsql.get(), options);

        osmdata_t osmdata(mid_pgsql, out_test);

        boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

        osmdata.start();

        parser->stream_file("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", &osmdata);

        parser.reset(nullptr);

        osmdata.stop();

        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'foobar_highways'");
        check_count(test_conn, 2753, "select count(*) from foobar_highways");

        //check that we have the right spread
        check_count(test_conn, 13, "select count(*) from foobar_highways where highway='bridleway'");
        check_count(test_conn, 3, "select count(*) from foobar_highways where highway='construction'");
        check_count(test_conn, 96, "select count(*) from foobar_highways where highway='cycleway'");
        check_count(test_conn, 249, "select count(*) from foobar_highways where highway='footway'");
        check_count(test_conn, 18, "select count(*) from foobar_highways where highway='living_street'");
        check_count(test_conn, 171, "select count(*) from foobar_highways where highway='path'");
        check_count(test_conn, 6, "select count(*) from foobar_highways where highway='pedestrian'");
        check_count(test_conn, 81, "select count(*) from foobar_highways where highway='primary'");
        check_count(test_conn, 842, "select count(*) from foobar_highways where highway='residential'");
        check_count(test_conn, 3, "select count(*) from foobar_highways where highway='road'");
        check_count(test_conn, 90, "select count(*) from foobar_highways where highway='secondary'");
        check_count(test_conn, 1, "select count(*) from foobar_highways where highway='secondary_link'");
        check_count(test_conn, 352, "select count(*) from foobar_highways where highway='service'");
        check_count(test_conn, 34, "select count(*) from foobar_highways where highway='steps'");
        check_count(test_conn, 33, "select count(*) from foobar_highways where highway='tertiary'");
        check_count(test_conn, 597, "select count(*) from foobar_highways where highway='track'");
        check_count(test_conn, 164, "select count(*) from foobar_highways where highway='unclassified'");
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
