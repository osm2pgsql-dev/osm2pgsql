#include "options.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

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
    std::shared_ptr<middle_t> mid = middle_t::create_middle(options.slim);
    if(dynamic_cast<middle_pgsql_t *>(mid.get()) == nullptr)
    {
        throw std::logic_error("Using slim mode we expected a pgsql middle");
    }

    const char* a2[] = {"osm2pgsql", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a2), const_cast<char **>(a2));
    mid = middle_t::create_middle(options.slim);
    if(dynamic_cast<middle_ram_t *>(mid.get()) == nullptr)
    {
        throw std::logic_error("Using without slim mode we expected a ram middle");
    }
}

void test_outputs()
{
    const char* a1[] = {"osm2pgsql", "-O", "pgsql", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t(len(a1), const_cast<char **>(a1));
    std::shared_ptr<middle_t> mid = middle_t::create_middle(options.slim);
    std::vector<std::shared_ptr<output_t> > outs = output_t::create_outputs(mid.get(), options);
    output_t* out = outs.front().get();
    if(dynamic_cast<output_pgsql_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a pgsql output");
    }

    const char* a2[] = {"osm2pgsql", "-O", "gazetteer", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a2), const_cast<char **>(a2));
    mid = middle_t::create_middle(options.slim);
    outs = output_t::create_outputs(mid.get(), options);
    out = outs.front().get();
    if(dynamic_cast<output_gazetteer_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a gazetteer output");
    }

    const char* a3[] = {"osm2pgsql", "-O", "null", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a3), const_cast<char **>(a3));
    mid = middle_t::create_middle(options.slim);
    outs = output_t::create_outputs(mid.get(), options);
    out = outs.front().get();
    if(dynamic_cast<output_null_t *>(out) == nullptr)
    {
        throw std::logic_error("Expected a null output");
    }

    const char* a4[] = {"osm2pgsql", "-O", "keine_richtige_ausgabe", "--style", "default.style", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t(len(a4), const_cast<char **>(a4));
    mid = middle_t::create_middle(options.slim);
    try
    {
        outs = output_t::create_outputs(mid.get(), options);
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
        std::unique_ptr<tagtransform_t> tagtransform =
            tagtransform_t::make_tagtransform(&options);
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

int get_random_proj(std::vector<std::string>& args)
{
    int proj = rand() % 3;
    switch(proj)
    {
        case 1:
            args.push_back("-l");
            return PROJ_LATLONG;
        case 2:
            args.push_back("-m");
            return PROJ_SPHERE_MERC;
    }

    args.push_back("--proj");
    //nice contiguous block of valid epsgs here randomly use one of those..
    proj = (rand() % (2962 - 2308)) + 2308;
    args.push_back((boost::format("%1%") % proj).str());
    return proj;
}

std::string get_random_string(const int length)
{
    std::string charset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890");
    std::string result;
    result.resize(length);

    for (int i = 0; i < length; i++)
        result[i] = charset[rand() % charset.length()];

    return result;
}

template<typename T>
void add_arg_or_not(const char* arg, std::vector<std::string>& args, T& option)
{
    if(rand() % 2)
    {
        args.push_back(arg);
        option = 1;
    }
    else
        option = 0;
}

void add_arg_and_val_or_not(const char* arg, std::vector<std::string>& args, int option, const int val)
{
    if(rand() % 2)
    {
        args.push_back(arg);
        args.push_back((boost::format("%1%") % val).str());
        option = val;
    }
}

void add_arg_and_val_or_not(const char* arg, std::vector<std::string>& args, const char *option, std::string val)
{
    if(rand() % 2)
    {
        args.push_back(arg);
        args.push_back(val);
        option = val.c_str();
    }
}

void test_random_perms()
{

    for(int i = 0; i < 5; ++i)
    {
        options_t options;
        std::vector<std::string> args;
        args.push_back("osm2pgsql");

        //pick a projection
        options.projection.reset(reprojection::create_projection(get_random_proj(args)));

        //pick a style file
        std::string style = get_random_string(15);
        options.style = style.c_str();
        args.push_back("--style");
        args.push_back(style);

        add_arg_and_val_or_not("--cache", args, options.cache, rand() % 800);
        if (options.database_options.db) {
            add_arg_and_val_or_not("--database", args, options.database_options.db->c_str(), get_random_string(6));
        }
        if (options.database_options.username) {
            add_arg_and_val_or_not("--username", args, options.database_options.username->c_str(), get_random_string(6));
        }
        if (options.database_options.host) {
            add_arg_and_val_or_not("--host", args, options.database_options.host->c_str(), get_random_string(6));
        }
        //add_arg_and_val_or_not("--port", args, options.port, rand() % 9999);

        //--hstore-match-only
        //--hstore-column   Add an additional hstore (key/value) column containing all tags that start with the specified string, eg --hstore-column "name:" will produce an extra hstore column that contains all name:xx tags

        add_arg_or_not("--hstore-add-index", args, options.enable_hstore_index);

        //--tablespace-index    The name of the PostgreSQL tablespace where all indexes will be created. The following options allow more fine-grained control:
        //      --tablespace-main-data    tablespace for main tables
        //      --tablespace-main-index   tablespace for main table indexes
        //      --tablespace-slim-data    tablespace for slim mode tables
        //      --tablespace-slim-index   tablespace for slim mode indexes
        //                    (if unset, use db's default; -i is equivalent to setting
        //                    --tablespace-main-index and --tablespace-slim-index)

        add_arg_and_val_or_not("--number-processes", args, options.num_procs, rand() % 12);

        //add_arg_or_not("--disable-parallel-indexing", args, options.parallel_indexing);

        add_arg_or_not("--unlogged", args, options.unlogged);

        //--cache-strategy  Specifies the method used to cache nodes in ram. Available options are: dense chunk sparse optimized

        if (options.flat_node_file) {
            add_arg_and_val_or_not("--flat-nodes", args, options.flat_node_file->c_str(), get_random_string(15));
        }

        //--expire-tiles [min_zoom-]max_zoom    Create a tile expiry list.

        add_arg_and_val_or_not("--expire-output", args, options.expire_tiles_filename.c_str(), get_random_string(15));

        //--bbox        Apply a bounding box filter on the imported data Must be specified as: minlon,minlat,maxlon,maxlat e.g. --bbox -0.5,51.25,0.5,51.75

        add_arg_and_val_or_not("--prefix", args, options.prefix.c_str(), get_random_string(15));

        //--input-reader    Input frontend. auto, o5m, xml, pbf

        if (options.tag_transform_script) {
            add_arg_and_val_or_not("--tag-transform-script", args, options.tag_transform_script->c_str(), get_random_string(15));
        }
        add_arg_or_not("--extra-attributes", args, options.extra_attributes);
        add_arg_or_not("--multi-geometry", args, options.enable_multi);
        add_arg_or_not("--keep-coastlines", args, options.keep_coastlines);

        //add the input file
        args.push_back("tests/liechtenstein-2013-08-03.osm.pbf");

        const char** argv = new const char*[args.size() + 1];
        argv[args.size()] = nullptr;
        for(std::vector<std::string>::const_iterator arg = args.begin(); arg != args.end(); ++arg)
            argv[arg - args.begin()] = arg->c_str();
        options_t((int) args.size(), const_cast<char **>(argv));
        delete[] argv;
    }
}

int main(int argc, char *argv[])
{
    srand(0);

    //try each test if any fail we will exit
    run_test("test_insufficient_args", test_insufficient_args);
    run_test("test_incompatible_args", test_incompatible_args);
    run_test("test_middles", test_middles);
    run_test("test_outputs", test_outputs);
    run_test("test_lua_styles", test_lua_styles);
    run_test("test_random_perms", test_random_perms);
    run_test("test_parsing_tile_expiry_zoom_levels_fails",
             test_parsing_tile_expiry_zoom_levels_fails);
    run_test("test_parsing_tile_expiry_zoom_levels",
             test_parsing_tile_expiry_zoom_levels);

    //passed
    return 0;
}
