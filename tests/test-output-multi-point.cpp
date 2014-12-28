#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "osmtypes.hpp"
#include "middle.hpp"
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
        boost::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
        options_t options;
        options.conninfo = db->conninfo().c_str();
        options.num_procs = 1;
        options.prefix = "osm2pgsql_test";
        options.tblsslim_index = "tablespacetest";
        options.tblsslim_data = "tablespacetest";
        options.slim = 1;

        boost::shared_ptr<geometry_processor> processor =
            geometry_processor::create("point", &options);

        export_list columns;
        { taginfo info; info.name = "amenity"; info.type = "text"; columns.add(OSMTYPE_NODE, info); }

        boost::shared_ptr<output_multi_t> out_test(new output_multi_t("foobar_amenities", processor, columns, mid_pgsql.get(), options));

        osmdata_t osmdata(mid_pgsql, out_test);

        boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection));

        osmdata.start();

        if (parser->streamFile("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", options.sanitize, &osmdata) != 0) {
            throw std::runtime_error("Unable to read input file `tests/liechtenstein-2013-08-03.osm.pbf'.");
        }

        parser.reset(NULL);

        osmdata.stop();

        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

        check_count(test_conn, 1,
                    "select count(*) from pg_catalog.pg_class "
                    "where relname = 'foobar_amenities'");

        check_count(test_conn, 244,
                    "select count(*) from foobar_amenities");

        check_count(test_conn, 36,
                    "select count(*) from foobar_amenities where amenity='parking'");

        check_count(test_conn, 34,
                    "select count(*) from foobar_amenities where amenity='bench'");

        check_count(test_conn, 1,
                    "select count(*) from foobar_amenities where amenity='vending_machine'");

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
