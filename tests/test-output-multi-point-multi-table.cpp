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

    if (PQresultStatus(res->get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error((boost::format("Query ERROR running %1%: %2%")
                                  % query % PQresultErrorMessage(res->get())).str());
    }

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
        options.scale = 10000000;
        options.num_procs = 1;
        options.prefix = "osm2pgsql_test";
        options.tblsslim_index = "tablespacetest";
        options.tblsslim_data = "tablespacetest";
        options.slim = 1;
        
        export_list columns;
        { taginfo info; info.name = "amenity"; info.type = "text"; columns.add(OSMTYPE_NODE, info); }
        
        std::vector<output_t*> outputs;

        // let's make lots of tables!
        for (int i = 0; i < 10; ++i) {
            std::string name = (boost::format("foobar_%d") % i).str();

            boost::shared_ptr<geometry_processor> processor =
                geometry_processor::create("point", &options);
        
            struct output_multi_t *out_test =
                new output_multi_t(name, processor, &columns, &mid_pgsql, options);

            outputs.push_back(out_test);
        }

        osmdata_t osmdata(&mid_pgsql, outputs);
        
        boost::scoped_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection));
        
        text_init();
        osmdata.start();

        if (parser->streamFile("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", options.sanitize, &osmdata) != 0) {
            throw std::runtime_error("Unable to read input file `tests/liechtenstein-2013-08-03.osm.pbf'.");
        }

        parser.reset(NULL);

        osmdata.stop();
        osmdata.cleanup();

        text_exit();
        
        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());
        
        for (int i = 0; i < 10; ++i) {
            std::string name = (boost::format("foobar_%d") % i).str();

            check_count(test_conn, 1, 
                        (boost::format("select count(*) from pg_catalog.pg_class "
                                       "where relname = 'osm2pgsql_test_foobar_%d'")
                         % i).str());
            
            check_count(test_conn, 244,
                        (boost::format("select count(*) from osm2pgsql_test_foobar_%d")
                         % i).str());
            
            check_count(test_conn, 36,
                        (boost::format("select count(*) from osm2pgsql_test_foobar_%d "
                                       "where amenity='parking'")
                         % i).str());
            
            check_count(test_conn, 34,
                        (boost::format("select count(*) from osm2pgsql_test_foobar_%d "
                                       "where amenity='bench'")
                         % i).str());
            
            check_count(test_conn, 1,
                        (boost::format("select count(*) from osm2pgsql_test_foobar_%d "
                                       "where amenity='vending_machine'")
                         % i).str());
        }

        return 0;
        
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        
    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }
    
    return 1;
}
