#ifndef TEST_COMMON_HPP
#define TEST_COMMON_HPP

#include "options.hpp"
#include "osmdata.hpp"
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

    template <typename MID>
    void run_osm2pgsql(options_t &options, char const *test_file,
                       char const *file_format)
    {
        //setup the middle
        auto middle = std::make_shared<MID>();

        //setup the backend (output)
        auto outputs = output_t::create_outputs(
            std::static_pointer_cast<middle_query_t>(middle), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(std::static_pointer_cast<middle_t>(middle), outputs);

        parse(test_file, file_format, options, &osmdata);
    }
}

#endif /* TEST_COMMON_HPP */
