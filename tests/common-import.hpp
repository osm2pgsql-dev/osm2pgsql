#ifndef OSM2PGSQL_TEST_COMMON_IMPORT_HPP
#define OSM2PGSQL_TEST_COMMON_IMPORT_HPP

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/osm/types_from_string.hpp>
#include <osmium/visitor.hpp>

#include "dependency-manager.hpp"
#include "geometry-processor.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "osmdata.hpp"
#include "output-multi.hpp"
#include "output.hpp"
#include "taginfo-impl.hpp"

#include "common-pg.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace testing {

inline void parse_file(options_t const &options,
                       std::unique_ptr<dependency_manager_t> dependency_manager,
                       std::shared_ptr<middle_t> const &mid,
                       std::vector<std::shared_ptr<output_t>> const &outs,
                       char const *filename = nullptr,
                       bool do_stop = true)
{
    osmdata_t osmdata{std::move(dependency_manager), mid, outs, options};

    osmdata.start();

    std::string filepath{TESTDATA_DIR};
    if (filename) {
        filepath += filename;
    } else {
        filepath += options.input_files[0];
    }
    osmium::io::File file{filepath};
    osmdata.process_file(file, options.bbox);

    if (do_stop) {
        osmdata.stop();
    }
}

/**
 * This is used as a helper to assemble OSM objects into an OPL file which
 * can later be used as input for testing.
 */
class data_t
{
public:
    data_t() = default;

    template <typename CONTAINER>
    data_t(CONTAINER const &objects)
    {
        std::copy(std::begin(objects), std::end(objects),
                  std::back_inserter(m_objects));
    }

    void add(char const *object) { m_objects.emplace_back(object); }

    template <typename CONTAINER>
    void add(CONTAINER const &objects)
    {
        std::copy(std::begin(objects), std::end(objects),
                  std::back_inserter(m_objects));
    }

    void add(std::initializer_list<const char *> const &objects)
    {
        std::copy(std::begin(objects), std::end(objects),
                  std::back_inserter(m_objects));
    }

    const char *operator()()
    {
        std::sort(m_objects.begin(), m_objects.end(),
                  [](std::string const &a, std::string const &b) {
                      return get_type_id(a) < get_type_id(b);
                  });

        m_result.clear();
        for (auto const &obj : m_objects) {
            assert(!obj.empty());
            m_result.append(obj);
            if (m_result.back() != '\n') {
                m_result += '\n';
            }
        }

        return m_result.c_str();
    }

private:
    static std::pair<osmium::item_type, osmium::object_id_type>
    get_type_id(std::string const &str)
    {
        std::string ti(str, 0, str.find(' '));
        return osmium::string_to_object_id(ti.c_str(),
                                           osmium::osm_entity_bits::nwr);
    }

    std::vector<std::string> m_objects;
    std::string m_result;
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
                    std::string const &format = "opl")
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

        auto dependency_manager = std::unique_ptr<dependency_manager_t>(
            options.with_forward_dependencies
                ? new full_dependency_manager_t{middle}
                : new dependency_manager_t{});

        osmdata_t osmdata{std::move(dependency_manager), middle, outputs,
                          options};

        osmdata.start();

        osmium::io::File file{data, std::strlen(data), format};
        osmdata.process_file(file, options.bbox);

        osmdata.stop();
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
