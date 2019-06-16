#ifndef OSM2PGSQL_TEST_COMMON_IMPORT_HPP
#define OSM2PGSQL_TEST_COMMON_IMPORT_HPP

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/visitor.hpp>

#include "middle-ram.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"

#include "common-pg.hpp"

namespace testing {
namespace db {

class test_parse_t : public parse_osmium_t
{
public:
    using parse_osmium_t::parse_osmium_t;

    void stream_buffer(char const *buf, std::string const &fmt)
    {
        osmium::io::File infile(buf, strlen(buf), fmt);

        osmium::io::Reader reader(infile);
        osmium::apply(reader, *this);
        reader.close();
    }
};

/**
 * Convenience class around tempdb_t that offers functions for
 * data import from file and strings.
 */
class import_t
{
public:
    void run_import(options_t options, char const *data,
                    std::string const &fmt = "opl")
    {
        options.database_options = m_db.db_options();

        // setup the middle
        std::shared_ptr<middle_t> middle(new middle_ram_t(&options));
        middle->start();

        // setup the output
        auto outputs = output_t::create_outputs(
            middle->get_query_instance(middle), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        osmdata.start();

        test_parse_t parser(options.bbox, options.append, &osmdata);

        parser.stream_buffer(data, fmt);

        osmdata.stop();
    }

    void run_file(options_t options, char const *file = nullptr)
    {
        options.database_options = m_db.db_options();

        // setup the middle
        std::shared_ptr<middle_t> middle(new middle_ram_t(&options));
        middle->start();

        // setup the output
        auto outputs = output_t::create_outputs(
            middle->get_query_instance(middle), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        osmdata.start();

        parse_osmium_t parser(options.bbox, options.append, &osmdata);

        std::string filep("tests/");
        if (file)
            filep += file;
        else
            filep += options.input_files[0];
        parser.stream_file(filep, options.input_reader);

        osmdata.stop();
    }

    pg::conn_t connect() { return m_db.connect(); }

private:
    pg::tempdb_t m_db;
};

} // namespace db
} // namespace testing

#endif
