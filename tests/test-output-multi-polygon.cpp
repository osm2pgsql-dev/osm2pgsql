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
#include "text-tree.hpp"

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
        struct middle_pgsql_t mid_pgsql;
        options_t options;
        options.conninfo = db->conninfo().c_str();
        options.num_procs = 1;
        options.prefix = "osm2pgsql_test";
        options.tblsslim_index = "tablespacetest";
        options.tblsslim_data = "tablespacetest";
        options.slim = 1;
        
        boost::shared_ptr<geometry_processor> processor = geometry_processor::create("polygon", &options);
        
        export_list columns;
        { taginfo info; info.name = "building"; info.type = "text"; columns.add(OSMTYPE_WAY, info); }
        
        struct output_multi_t out_test("foobar_buildings", processor, &columns, &mid_pgsql, options);
        
        osmdata_t osmdata(&mid_pgsql, &out_test);
        
        boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection));
        
        text_init();
        osmdata.start();

        if (parser->streamFile("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", options.sanitize, &osmdata) != 0) {
            throw std::runtime_error("Unable to read input file `tests/liechtenstein-2013-08-03.osm.pbf'.");
        }

        parser.reset(NULL);

        osmdata.stop();

        text_exit();
        
        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'osm2pgsql_test_foobar_buildings'");
        check_count(test_conn, 3723, "select count(*) from osm2pgsql_test_foobar_buildings");

        //check that we have the right spread
        check_count(test_conn, 1, "select count(*) from osm2pgsql_test_foobar_buildings where building='barn'");
        check_count(test_conn, 1, "select count(*) from osm2pgsql_test_foobar_buildings where building='chapel'");
        check_count(test_conn, 5, "select count(*) from osm2pgsql_test_foobar_buildings where building='church'");
        check_count(test_conn, 3, "select count(*) from osm2pgsql_test_foobar_buildings where building='commercial'");
        check_count(test_conn, 6, "select count(*) from osm2pgsql_test_foobar_buildings where building='farm'");
        check_count(test_conn, 1, "select count(*) from osm2pgsql_test_foobar_buildings where building='garage'");
        check_count(test_conn, 2, "select count(*) from osm2pgsql_test_foobar_buildings where building='glasshouse'");
        check_count(test_conn, 1, "select count(*) from osm2pgsql_test_foobar_buildings where building='greenhouse'");
        check_count(test_conn, 153, "select count(*) from osm2pgsql_test_foobar_buildings where building='house'");
        check_count(test_conn, 4, "select count(*) from osm2pgsql_test_foobar_buildings where building='hut'");
        check_count(test_conn, 8, "select count(*) from osm2pgsql_test_foobar_buildings where building='industrial'");
        check_count(test_conn, 200, "select count(*) from osm2pgsql_test_foobar_buildings where building='residential'");
        check_count(test_conn, 6, "select count(*) from osm2pgsql_test_foobar_buildings where building='roof'");
        check_count(test_conn, 4, "select count(*) from osm2pgsql_test_foobar_buildings where building='school'");
        check_count(test_conn, 2, "select count(*) from osm2pgsql_test_foobar_buildings where building='station'");
        check_count(test_conn, 3, "select count(*) from osm2pgsql_test_foobar_buildings where building='warehouse'");
        check_count(test_conn, 3323, "select count(*) from osm2pgsql_test_foobar_buildings where building='yes'");
        return 0;
        
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        
    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }
    
    return 1;
}
