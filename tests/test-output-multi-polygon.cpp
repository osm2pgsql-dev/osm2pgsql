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

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"
#include "tests/common.hpp"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        options_t options;
        options.database_options = db->database_options;
        options.num_procs = 1;
        options.prefix = "osm2pgsql_test";
        options.slim = true;

        std::shared_ptr<geometry_processor> processor = geometry_processor::create("polygon", &options);

        export_list columns;
        {
            taginfo info;
            info.name = "building";
            info.type = "text";
            columns.add(osmium::item_type::way, info);
        }

        std::shared_ptr<middle_t> mid_pgsql(new middle_pgsql_t(&options));
        mid_pgsql->start();
        auto midq = mid_pgsql->get_query_instance(mid_pgsql);

        auto out_test = std::make_shared<output_multi_t>(
            "foobar_buildings", processor, columns, midq, options,
            std::make_shared<db_copy_thread_t>(
                options.database_options.conninfo()));

        osmdata_t osmdata(mid_pgsql, out_test);

        testing::parse("tests/liechtenstein-2013-08-03.osm.pbf", "pbf",
                       options, &osmdata);

        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'foobar_buildings'");
        db->check_count(0, "select count(*) from foobar_buildings where building is null");
        db->check_count(3723, "select count(*) from foobar_buildings");

        //check that we have the right spread
        db->check_count(1, "select count(*) from foobar_buildings where building='barn'");
        db->check_count(1, "select count(*) from foobar_buildings where building='chapel'");
        db->check_count(5, "select count(*) from foobar_buildings where building='church'");
        db->check_count(3, "select count(*) from foobar_buildings where building='commercial'");
        db->check_count(6, "select count(*) from foobar_buildings where building='farm'");
        db->check_count(1, "select count(*) from foobar_buildings where building='garage'");
        db->check_count(2, "select count(*) from foobar_buildings where building='glasshouse'");
        db->check_count(1, "select count(*) from foobar_buildings where building='greenhouse'");
        db->check_count(153, "select count(*) from foobar_buildings where building='house'");
        db->check_count(4, "select count(*) from foobar_buildings where building='hut'");
        db->check_count(8, "select count(*) from foobar_buildings where building='industrial'");
        db->check_count(200, "select count(*) from foobar_buildings where building='residential'");
        db->check_count(6, "select count(*) from foobar_buildings where building='roof'");
        db->check_count(4, "select count(*) from foobar_buildings where building='school'");
        db->check_count(2, "select count(*) from foobar_buildings where building='station'");
        db->check_count(3, "select count(*) from foobar_buildings where building='warehouse'");
        db->check_count(3323, "select count(*) from foobar_buildings where building='yes'");
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
