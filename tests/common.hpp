#ifndef TEST_COMMON_HPP
#define TEST_COMMON_HPP

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"

namespace testing {

    void parse(const char *filename, const char *format,
                const options_t &options, osmdata_t *osmdata)
    {
        parse_osmium_t parser(options.bbox, options.append, osmdata);

        osmdata->start();
        parser.stream_file(filename, format);
        osmdata->stop();
    }

    void run_osm2pgsql(options_t &options, char const *test_file,
                       char const *file_format)
    {
        //setup the middle
        std::shared_ptr<middle_t> middle;

        if (options.slim) {
            middle = std::shared_ptr<middle_t>(new middle_pgsql_t(&options));
        } else {
            middle = std::shared_ptr<middle_t>(new middle_ram_t(&options));
        }

        middle->start();

        //setup the backend (output)
        auto outputs = output_t::create_outputs(
            middle->get_query_instance(middle), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        parse(test_file, file_format, options, &osmdata);
    }
}

#endif /* TEST_COMMON_HPP */
