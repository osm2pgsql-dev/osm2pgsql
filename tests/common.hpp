#ifndef TEST_COMMON_HPP
#define TEST_COMMON_HPP

#include "options.hpp"
#include "osmdata.hpp"
#include "parse-osmium.hpp"

namespace testing {

    void parse(const char *filename, const char *format,
                const options_t &options, osmdata_t *osmdata)
    {
        parse_osmium_t parser(options.extra_attributes, options.bbox,
                              options.projection.get(), options.append, osmdata);

        osmdata->start();
        parser.stream_file(filename, format);
        osmdata->stop();
    }
}

#endif /* TEST_COMMON_HPP */
