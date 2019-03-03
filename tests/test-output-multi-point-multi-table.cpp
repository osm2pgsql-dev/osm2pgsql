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

        export_list columns;
        {
            taginfo info;
            info.name = "amenity";
            info.type = "text";
            columns.add(osmium::item_type::node, info);
        }

        std::shared_ptr<middle_t> mid_pgsql(new middle_pgsql_t(&options));
        mid_pgsql->start();
        auto midq = mid_pgsql->get_query_instance(mid_pgsql);

        std::vector<std::shared_ptr<output_t> > outputs;

        // let's make lots of tables!
        for (int i = 0; i < 10; ++i) {
            std::string name = (boost::format("foobar_%d") % i).str();

            std::shared_ptr<geometry_processor> processor =
                geometry_processor::create("point", &options);

            auto out_test = std::make_shared<output_multi_t>(
                name, processor, columns, midq, options,
                std::make_shared<db_copy_thread_t>(
                    options.database_options.conninfo()));

            outputs.push_back(out_test);
        }

        osmdata_t osmdata(mid_pgsql, outputs);

        testing::parse("tests/liechtenstein-2013-08-03.osm.pbf", "pbf",
                       options, &osmdata);

        for (int i = 0; i < 10; ++i) {
            std::string name = (boost::format("foobar_%d") % i).str();

            db->check_count(1,
                        (boost::format("select count(*) from pg_catalog.pg_class "
                                       "where relname = 'foobar_%d'")
                         % i).str());

            db->check_count(244,
                        (boost::format("select count(*) from foobar_%d")
                         % i).str());

            db->check_count(36,
                        (boost::format("select count(*) from foobar_%d "
                                       "where amenity='parking'")
                         % i).str());

            db->check_count(34,
                        (boost::format("select count(*) from foobar_%d "
                                       "where amenity='bench'")
                         % i).str());

            db->check_count(1,
                        (boost::format("select count(*) from foobar_%d "
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
