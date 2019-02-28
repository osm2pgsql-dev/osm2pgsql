#include "options.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"
#include "taginfo_impl.hpp"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "tests/common-pg.hpp"

namespace alg = boost::algorithm;

#define len(x) sizeof(x)/sizeof(x[0])

void run_test(const char* test_name, void (*testfunc)())
{
    try
    {
        fprintf(stderr, "%s\n", test_name);
        testfunc();
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "FAIL\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "PASS\n");
}

void parse_fail(const int argc, const char* argv[], const std::string& fail_message)
{
    try
    {
        options_t options = options_t(argc, const_cast<char **>(argv));
        throw std::logic_error((boost::format("Expected '%1%'") % fail_message).str());
    }
    catch(const std::runtime_error& e)
    {
        if(!alg::icontains(e.what(), fail_message))
            throw std::logic_error((boost::format("Expected '%1%' but instead got '%2%'") % fail_message % e.what()).str());
    }
}

void test_insufficient_args()
{
    const char* argv[] = {"osm2pgsql", "-a", "-c", "--slim"};
    parse_fail(len(argv), argv, "usage error");
}

void test_incompatible_args()
{
    const char* a1[] = {"osm2pgsql", "-a", "-c", "--slim", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a1), a1, "options can not be used at the same time");

    const char* a2[] = {"osm2pgsql", "--drop", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a2), a2, "drop only makes sense with");

    const char* a3[] = {"osm2pgsql", "-j", "-k", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a3), a3, "you can not specify both");
}

void test_middles()
{
    const char* a1[] = {"osm2pgsql", "--slim", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t(len(a1), const_cast<char **>(a1));
    if (!options.slim) {
        throw std::logic_error("Using slim mode expected");
    }

    const char* a2[] = {"osm2pgsql", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a2), const_cast<char **>(a2));
    if (options.slim) {
        throw std::logic_error("Using without slim mode expected");
    }
}

void test_outputs()
{
    pg::tempdb db;
    const char* a1[] = {"osm2pgsql", "-d", db.database_options.db->c_str(), "-O", "pgsql", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t(len(a1), const_cast<char **>(a1));
    auto middle_ram = std::make_shared<middle_ram_t>(&options);
    auto mid = middle_ram->get_query_instance(middle_ram);
    auto outs = output_t::create_outputs(mid, options);
    output_t* out = outs.front().get();
    if(dynamic_cast<output_pgsql_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a pgsql output");
    }

    const char* a2[] = {"osm2pgsql", "-d", db.database_options.db->c_str(), "-O", "gazetteer", "--style", "tests/gazetteer-test.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a2), const_cast<char **>(a2));
    outs = output_t::create_outputs(mid, options);
    out = outs.front().get();
    if(dynamic_cast<output_gazetteer_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a gazetteer output");
    }

    const char* a3[] = {"osm2pgsql", "-d", db.database_options.db->c_str(), "-O", "null", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a3), const_cast<char **>(a3));
    outs = output_t::create_outputs(mid, options);
    out = outs.front().get();
    if(dynamic_cast<output_null_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a null output");
    }

    const char* a4[] = {"osm2pgsql", "-d", db.database_options.db->c_str(), "-O", "keine_richtige_ausgabe", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a4), const_cast<char **>(a4));
    try
    {
        outs = output_t::create_outputs(mid, options);
        out = outs.front().get();
        throw std::logic_error("Expected 'not recognised'");
    }
    catch(const std::runtime_error& e)
    {
        if(!alg::icontains(e.what(), "not recognised"))
            throw std::logic_error((boost::format("Expected 'not recognised' but instead got '%2%'") % e.what()).str());
    }
}

void test_lua_styles()
{
#ifdef HAVE_LUA  
    const char *a1[] = {"osm2pgsql", "--tag-transform-script",
                        "non_existing.lua",
                        "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t(len(a1), const_cast<char **>(a1));

    try {
        export_list exlist;
        std::unique_ptr<tagtransform_t> tagtransform =
            tagtransform_t::make_tagtransform(&options, exlist);
        throw std::logic_error("Expected 'No such file or directory'");
    } catch (const std::runtime_error &e) {
        if (!alg::icontains(e.what(), "No such file or directory"))
            throw std::logic_error(
                (boost::format("Expected 'No such file or directory' but "
                               "instead got '%1%'") %
                 e.what())
                    .str());
    }
#endif
}

void test_parsing_tile_expiry_zoom_levels()
{
    const char *a1[] = {
        "osm2pgsql",     "-e",
        "8-12",          "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t(len(a1), const_cast<char **>(a1));
    if (options.expire_tiles_zoom_min != 8)
        throw std::logic_error(
            (boost::format("Expected expire_tiles_zoom_min 8 but got '%1%'") %
             options.expire_tiles_zoom_min)
                .str());
    if (options.expire_tiles_zoom != 12)
        throw std::logic_error(
            (boost::format("Expected expire_tiles_zoom 12 but got '%1%'") %
             options.expire_tiles_zoom_min)
                .str());

    const char *a2[] = {"osm2pgsql",
                        "-e",
                        "12",
                        "--style",
                        "default.style",
                        "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a2), const_cast<char **>(a2));
    if (options.expire_tiles_zoom_min != 12)
        throw std::logic_error(
            (boost::format("Expected expire_tiles_zoom_min 12 but got '%1%'") %
             options.expire_tiles_zoom_min)
                .str());
    if (options.expire_tiles_zoom != 12)
        throw std::logic_error(
            (boost::format("Expected expire_tiles_zoom 12 but got '%1%'") %
             options.expire_tiles_zoom_min)
                .str());

    //  check if very high zoom levels are set back to still high but valid zoom levels
    const char *a3[] = {
        "osm2pgsql",     "-e",
        "33-35",         "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a3), const_cast<char **>(a3));
    if (options.expire_tiles_zoom_min != 31)
        throw std::logic_error((boost::format("Expected expire_tiles_zoom_min "
                                              "set back to 31 but got '%1%'") %
                                options.expire_tiles_zoom_min)
                                   .str());
    if (options.expire_tiles_zoom != 31)
        throw std::logic_error(
            (boost::format(
                 "Expected expire_tiles_zoom set back to 31 but got '%1%'") %
             options.expire_tiles_zoom_min)
                .str());
}

void test_parsing_tile_expiry_zoom_levels_fails()
{
    const char *a1[] = {
        "osm2pgsql",     "-e",
        "8--12",         "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a1), a1, "Invalid maximum zoom level given for tile expiry");

    const char *a2[] = {
        "osm2pgsql",     "-e",
        "-8-12",         "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a2), a2,
               "Missing argument for option --expire-tiles. Zoom levels must be positive.");

    const char *a3[] = {"osm2pgsql", "-e", "--style", "default.style",
                        "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a3), a3,
               "Missing argument for option --expire-tiles. Zoom levels must be positive.");

    const char *a4[] = {
        "osm2pgsql",     "-e",
        "a-8",           "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a4), a4, "Bad argument for option --expire-tiles. Minimum zoom level must be larger than 0.");

    const char *a5[] = {
        "osm2pgsql",     "-e",
        "6:8",           "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a5), a5, "Minimum and maximum zoom level for tile expiry "
                            "must be separated by '-'.");

    const char *a6[] = {
        "osm2pgsql",     "-e",
        "6-0",           "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a6), a6,
               "Invalid maximum zoom level given for tile expiry.");

    const char *a7[] = {
        "osm2pgsql",     "-e",
        "6-9a",          "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a7), a7,
               "Invalid maximum zoom level given for tile expiry.");

    const char *a8[] = {
        "osm2pgsql",     "-e",
        "0-8",           "--style",
        "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a8), a8,
               "Bad argument for option --expire-tiles. Minimum zoom level "
               "must be larger than 0.");

    const char *a9[] = {"osm2pgsql",
                        "-e",
                        "6-",
                        "--style",
                        "default.style",
                        "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a9), a9,
               "Invalid maximum zoom level given for tile "
               "expiry.");

    const char *a10[] = {"osm2pgsql",
                         "-e",
                         "-6",
                         "--style",
                         "default.style",
                         "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a10), a10,
               "Missing argument for option --expire-tiles. Zoom levels "
               "must be positive.");

    const char *a11[] = {"osm2pgsql",
                         "-e",
                         "0",
                         "--style",
                         "default.style",
                         "tests/liechtenstein-2013-08-03.osm.pbf"};
    parse_fail(len(a11), a11,
               "Bad argument for option --expire-tiles. Minimum zoom level "
               "must be larger than 0.");
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    srand(0);

    //try each test if any fail we will exit
    run_test("test_insufficient_args", test_insufficient_args);
    run_test("test_incompatible_args", test_incompatible_args);
    run_test("test_middles", test_middles);
    run_test("test_outputs", test_outputs);
    run_test("test_lua_styles", test_lua_styles);
    run_test("test_parsing_tile_expiry_zoom_levels_fails",
             test_parsing_tile_expiry_zoom_levels_fails);
    run_test("test_parsing_tile_expiry_zoom_levels",
             test_parsing_tile_expiry_zoom_levels);

    //passed
    return 0;
}
