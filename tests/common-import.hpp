#ifndef OSM2PGSQL_TEST_COMMON_IMPORT_HPP
#define OSM2PGSQL_TEST_COMMON_IMPORT_HPP

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/visitor.hpp>

#include "dependency-manager.hpp"
#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"
#include "taginfo-impl.hpp"

#include "common-pg.hpp"

namespace testing {

inline void parse_file(options_t const &options,
                       std::unique_ptr<dependency_manager_t> dependency_manager,
                       std::shared_ptr<middle_t> const &mid,
                       std::vector<std::shared_ptr<output_t>> const &outs,
                       char const *filename = nullptr)
{
    osmdata_t osmdata{std::move(dependency_manager), mid, outs, options};

    parse_osmium_t parser{options.bbox, options.append, &osmdata};

    std::string filep{TESTDATA_DIR};
    filep += filename ? filename : options.input_files[0];

    parser.stream_file(filep, "");

    osmdata.process();
}

class test_parse_t : public parse_osmium_t
{
public:
    using parse_osmium_t::parse_osmium_t;

    void stream_buffer(char const *buf, std::string const &fmt)
    {
        osmium::io::File infile{buf, std::strlen(buf), fmt};

        osmium::io::Reader reader{infile};
        osmium::apply(reader, *this);
        reader.close();
    }
};

namespace db {

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

        std::shared_ptr<middle_t> middle;

        if (options.slim) {
            middle = std::shared_ptr<middle_t>(new middle_pgsql_t{&options});
        } else {
            middle = std::shared_ptr<middle_t>(new middle_ram_t{&options});
        }
        middle->start();

        auto const outputs =
            output_t::create_outputs(middle->get_query_instance(), options);

        bool const need_dependencies =
            std::any_of(outputs.cbegin(), outputs.cend(),
                        [](std::shared_ptr<output_t> const &output) {
                            return output->need_forward_dependencies();
                        });

        auto dependency_manager = std::unique_ptr<dependency_manager_t>(
            need_dependencies ? new full_dependency_manager_t{middle}
                              : new dependency_manager_t{});

        osmdata_t osmdata{std::move(dependency_manager), middle, outputs,
                          options};

        test_parse_t parser(options.bbox, options.append, &osmdata);

        parser.stream_buffer(data, fmt);

        osmdata.process();
    }

    void run_file(options_t options, char const *file = nullptr)
    {
        options.database_options = m_db.db_options();

        auto middle = std::make_shared<middle_ram_t>(&options);
        middle->start();

        auto const outputs =
            output_t::create_outputs(middle->get_query_instance(), options);

        auto dependency_manager = std::unique_ptr<dependency_manager_t>(
            new full_dependency_manager_t{middle});

        parse_file(options, std::move(dependency_manager), middle, outputs,
                   file);
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

        auto mid_pgsql = std::make_shared<middle_pgsql_t>(&options);
        mid_pgsql->start();
        auto const midq = mid_pgsql->get_query_instance();

        // This actually uses the multi-backend with C transforms,
        // not Lua transforms. This is unusual and doesn't reflect real practice.
        auto const out_test = std::make_shared<output_multi_t>(
            table_name, proc, columns, midq, options,
            std::make_shared<db_copy_thread_t>(
                options.database_options.conninfo()));

        auto dependency_manager = std::unique_ptr<dependency_manager_t>(
            new full_dependency_manager_t{mid_pgsql});

        parse_file(options, std::move(dependency_manager), mid_pgsql,
                   {out_test}, file);
    }

    pg::conn_t connect() { return m_db.connect(); }

    pg::tempdb_t const &db() const { return m_db; }

private:
    pg::tempdb_t m_db;
};

} // namespace db
} // namespace testing

#endif // OSM2PGSQL_TEST_COMMON_IMPORT_HPP
