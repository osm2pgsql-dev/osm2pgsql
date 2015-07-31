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

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

int main(int argc, char *argv[]) {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        std::shared_ptr<middle_pgsql_t> mid_pgsql(new middle_pgsql_t());
        options_t options;
        options.database_options = db->database_options;
        options.num_procs = 1;
        options.prefix = "osm2pgsql_test";
        options.slim = true;

        std::shared_ptr<geometry_processor> processor =
            geometry_processor::create("point", &options);

        export_list columns;
        { taginfo info; info.name = "amenity"; info.type = "text"; columns.add(OSMTYPE_NODE, info); }

        auto out_test = std::make_shared<output_multi_t>("foobar_amenities", processor, columns, mid_pgsql.get(), options);

        osmdata_t osmdata(mid_pgsql, out_test);

        std::unique_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, options.append));

        osmdata.start();

        parser->stream_file("pbf", "tests/liechtenstein-2013-08-03.osm.pbf", &osmdata);

        parser.reset(nullptr);

        osmdata.stop();

        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->database_options);

        db->check_count(1,
                    "select count(*) from pg_catalog.pg_class "
                    "where relname = 'foobar_amenities'");

        db->check_count(244,
                    "select count(*) from foobar_amenities");

        db->check_count(36,
                    "select count(*) from foobar_amenities where amenity='parking'");

        db->check_count(34,
                    "select count(*) from foobar_amenities where amenity='bench'");

        db->check_count(1,
                    "select count(*) from foobar_amenities where amenity='vending_machine'");

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
