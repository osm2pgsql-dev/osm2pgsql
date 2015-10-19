/*

Test the hstore-match-only functionality of osm2pgsql in a hstore only database

The tags of inteest are specified in hstore-match-only.style

*/
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
#include "middle.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "taginfo_impl.hpp"
#include "parse.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

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
        options.style="tests/hstore-match-only.style";
        options.global_table_options.hstore_match_only=1;
        options.global_table_options.hstore_mode = HSTORE_NORM;
        options.slim = 1;

        auto out_test = std::make_shared<output_pgsql_t>(mid_pgsql.get(), options);

        osmdata_t osmdata(mid_pgsql, out_test);

        std::unique_ptr<parse_delegate_t> parser(new parse_delegate_t(options.extra_attributes, options.bbox, options.projection, false));

        osmdata.start();

        parser->stream_file("xml", "tests/hstore-match-only.osm", &osmdata);

        parser.reset(nullptr);

        osmdata.stop();

        // tables should not contain any tag columns
        db->check_count(4, "select count(column_name) from information_schema.columns where table_name='osm2pgsql_test_point'");
        db->check_count(5, "select count(column_name) from information_schema.columns where table_name='osm2pgsql_test_polygon'");
        db->check_count(5, "select count(column_name) from information_schema.columns where table_name='osm2pgsql_test_line'");
        db->check_count(5, "select count(column_name) from information_schema.columns where table_name='osm2pgsql_test_roads'");
        
        // the testfile contains 19 tagged ways and 7 tagged nodes
        // out of them 18 ways and 6 nodes are interesting as specified by hstore-match-only.style
        // as there is also one relation we should end up getting a database which contains:
        // 6 objects in osm2pgsql_test_point
        // 7 objects in osm2pgsql_test_polygon
        // 12 objects in osm2pgsql_test_line
        // 3 objects in osm2pgsql_test_roads
        
        db->check_count(6, "select count(*) from osm2pgsql_test_point");
        db->check_count(7, "select count(*) from osm2pgsql_test_polygon");
        db->check_count(12, "select count(*) from osm2pgsql_test_line");
        db->check_count(3, "select count(*) from osm2pgsql_test_roads");
        
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
