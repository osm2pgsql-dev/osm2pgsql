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
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

namespace alg = boost::algorithm;

#define len(x) sizeof(x)/sizeof(x[0])

void run_test(void (*testfunc)())
{
    try
    {
        testfunc();
    }
    catch(std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(EXIT_FAILURE);
    }
}

void parse_fail(const int argc, const char* argv[], const std::string& fail_message)
{
    try
    {
        options_t options = options_t::parse(argc, const_cast<char **>(argv));
        throw std::logic_error((boost::format("Expected '%1%'") % fail_message).str());
    }
    catch(std::runtime_error& e)
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
    options_t options = options_t::parse(len(a1), const_cast<char **>(a1));
    middle_t* mid = options.create_middle();
    if(dynamic_cast<middle_pgsql_t *>(mid) == NULL)
    {
        throw std::logic_error("Using slim mode we expected a pgsql middle");
    }

    const char* a2[] = {"osm2pgsql", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t::parse(len(a2), const_cast<char **>(a2));
    mid = options.create_middle();
    if(dynamic_cast<middle_ram_t *>(mid) == NULL)
    {
        throw std::logic_error("Using without slim mode we expected a ram middle");
    }
}

void test_outputs()
{
    const char* a1[] = {"osm2pgsql", "-O", "pgsql", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options_t options = options_t::parse(len(a1), const_cast<char **>(a1));
    middle_t* mid = options.create_middle();
    output_t* out = options.create_output(mid);
    if(dynamic_cast<output_pgsql_t *>(out) == NULL)
    {
        throw std::logic_error("Expected a pgsql output");
    }

    const char* a2[] = {"osm2pgsql", "-O", "gazetteer", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t::parse(len(a2), const_cast<char **>(a2));
    mid = options.create_middle();
    out = options.create_output(mid);
    if(dynamic_cast<output_gazetteer_t *>(out) == NULL)
    {
        throw std::logic_error("Expected a gazetteer output");
    }

    const char* a3[] = {"osm2pgsql", "-O", "null", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t::parse(len(a3), const_cast<char **>(a3));
    mid = options.create_middle();
    out = options.create_output(mid);
    if(dynamic_cast<output_null_t *>(out) == NULL)
    {
        throw std::logic_error("Expected a null output");
    }

    const char* a4[] = {"osm2pgsql", "-O", "keine_richtige_ausgabe", "tests/liechtenstein-2013-08-03.osm.pbf"};
    options = options_t::parse(len(a4), const_cast<char **>(a4));
    mid = options.create_middle();
    try
    {
        out = options.create_output(mid);
        throw std::logic_error("Expected 'not recognised'");
    }
    catch(std::runtime_error& e)
    {
        if(!alg::icontains(e.what(), "not recognised"))
            throw std::logic_error((boost::format("Expected 'not recognised' but instead got '%2%'") % e.what()).str());
    }
}
/*
std::string get_random_proj(int& proj)
{
    switch(rand() % (PROJ_COUNT + 1))
    {
    case PROJ_LATLONG:
        proj = PROJ_LATLONG;
        return "--latlong";
    case PROJ_MERC:
        proj = PROJ_MERC;
        return "--oldmerc";
    case PROJ_SPHERE_MERC:
        proj = PROJ_SPHERE_MERC;
        return "--merc";
    default:
        proj = -((rand() % 9000) + 1000);
        return (boost::format("--proj %1%") % (-proj)).str();
    }
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

std::string get_random_numstr()
{
    return (boost::format("%1%") % ((rand() % 9000) + 1000)).str();
}

void add_arg_or_not(const char* arg, std::string& args, int& option)
{
    if(rand() % 2)
    {
        args.push_back(' ');
        args.append(arg);
        option = 1;
    }
    else
        option = 0;
}

void add_arg_and_val_or_not(const char* arg, std::string& args, int& option, const int val)
{
    if(rand() % 2)
    {
        args.append((boost::format(" %1% %2%") % arg % val).str());
        option = val;
    }
}

void add_arg_and_val_or_not(const char* arg, std::string& args, const char*& option, std::string val)
{
    if(rand() % 2)
    {
        args.append((boost::format(" %1% %2%") % arg % val).str());
        option = val.c_str();
    }
}
*/
void test_random_perms()
{

    /*for(int i = 0; i < 5; ++i)
    {
        options_t options;
        std::string args = "osm2pgsql";

        //pick a projection
        args.push_back(' ');
        int proj;
        args.append(get_random_proj(proj));
        options.projection.reset(new reprojection(proj));

        //pick a style file
        args.push_back(' ');
        std::string style = get_random_string(15);
        args.append("--style " + style);
        options.style = style;

        add_arg_and_val_or_not("--cache", args, options.cache, rand() % 800);
        add_arg_and_val_or_not("--database", args, options.db, get_random_string(6));
        add_arg_and_val_or_not("--username", args, options.username, get_random_string(6));
        add_arg_and_val_or_not("--host", args, options.host, get_random_string(6));
        //add_arg_and_val_or_not("--port", args, options.port, rand() % 9999);

        //--hstore-match-only
        //--hstore-column   Add an additional hstore (key/value) column containing all tags that start with the specified string, eg --hstore-column "name:" will produce an extra hstore column that contains all name:xx tags

        add_arg_or_not("--hstore-add-index", args, options.enable_hstore_index);
        add_arg_or_not("--utf8-sanitize", args, options.sanitize);

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

        add_arg_and_val_or_not("--flat-nodes", args, options.flat_node_file, get_random_string(15));

        //--expire-tiles [min_zoom-]max_zoom    Create a tile expiry list.

        add_arg_and_val_or_not("--expire-output", args, options.expire_tiles_filename, get_random_string(15));

        //--bbox        Apply a bounding box filter on the imported data Must be specified as: minlon,minlat,maxlon,maxlat e.g. --bbox -0.5,51.25,0.5,51.75

        add_arg_and_val_or_not("--prefix", args, options.prefix, get_random_string(15));

        //--input-reader    Input frontend. libxml2   - Parse XML using libxml2. (default) primitive - Primitive XML parsing. pbf       - OSM binary format.

        add_arg_and_val_or_not("--tag-transform-script", args, options.tag_transform_script, get_random_string(15));
        add_arg_or_not("--extra-attributes", args, options.extra_attributes);
        add_arg_or_not("--multi-geometry", args, options.enable_multi);
        add_arg_or_not("--keep-coastlines", args, options.keep_coastlines);
        add_arg_or_not("--exclude-invalid-polygon", args, options.excludepoly);

        std::vector<std::string> split;
        boost::split(split, args, boost::is_any_of(" "));

        char* argv[] = new char*[split.size()];
        options_t::parse(split.size(), );
        delete[] argv;
    }*/
}

int main(int argc, char *argv[])
{
    srand(0);

    //try each test if any fail we will exit
    run_test(test_insufficient_args);
    run_test(test_incompatible_args);
    run_test(test_middles);
    run_test(test_outputs);
    run_test(test_random_perms);

    //passed
    return 0;
}
