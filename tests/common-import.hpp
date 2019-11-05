#ifndef OSM2PGSQL_TEST_COMMON_IMPORT_HPP
#define OSM2PGSQL_TEST_COMMON_IMPORT_HPP

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/visitor.hpp>

#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"
#include "taginfo_impl.hpp"

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

        std::string filep(TESTDATA_DIR);
        if (file)
            filep += file;
        else
            filep += options.input_files[0];
        parser.stream_file(filep, options.input_reader);

        osmdata.stop();
    }

    void run_file_multi_output(options_t options,
                               std::shared_ptr<geometry_processor> const &proc,
                               char const *table_name, osmium::item_type type,
                               char const *tag_key, char const *file)
    {
        options.database_options = m_db.db_options();

        export_list columns;
        {
            taginfo info;
            info.name = tag_key;
            info.type = "text";
            columns.add(type, info);
        }

        std::shared_ptr<middle_t> mid_pgsql(new middle_pgsql_t(&options));
        mid_pgsql->start();
        auto midq = mid_pgsql->get_query_instance(mid_pgsql);

        // This actually uses the multi-backend with C transforms,
        // not Lua transforms. This is unusual and doesn't reflect real practice.
        auto out_test = std::make_shared<output_multi_t>(
            table_name, proc, columns, midq, options,
            std::make_shared<db_copy_thread_t>(
                options.database_options.conninfo()));

        std::string filep(TESTDATA_DIR);
        filep += file;

        osmdata_t osmdata(mid_pgsql, out_test);
        osmdata.start();
        parse_osmium_t parser(options.bbox, options.append, &osmdata);
        parser.stream_file(filep.c_str(), "");
        osmdata.stop();
    }

    pg::conn_t connect() { return m_db.connect(); }

    pg::tempdb_t const &db() const { return m_db; }

private:
    pg::tempdb_t m_db;
};

} // namespace db
} // namespace testing

#endif
