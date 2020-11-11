#include "config.h"

#include "db-copy.hpp"
#include "format.hpp"
#include "output-gazetteer.hpp"
#include "output-multi.hpp"
#include "output-null.hpp"
#include "output-pgsql.hpp"
#include "output.hpp"
#include "taginfo-impl.hpp"

#ifdef HAVE_LUA
# include "output-flex.hpp"
# define flex_backend "flex, "
#else
# define flex_backend ""
#endif

#include <cstring>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static std::string adapt_relative_filename(std::string const &lua_file,
                                           std::string const &style_file)
{
    boost::filesystem::path base_path{style_file};
    return boost::filesystem::absolute(lua_file, base_path.parent_path())
        .string();
}

namespace pt = boost::property_tree;

namespace {

template <typename T>
void override_if(T &t, std::string const &key, pt::ptree const &conf)
{
    boost::optional<T> opt = conf.get_optional<T>(key);
    if (opt) {
        t = *opt;
    }
}

std::shared_ptr<output_t>
parse_multi_single(pt::ptree const &conf,
                   std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread)
{
    options_t new_opts = options;

    auto const name = conf.get<std::string>("name");
    auto const proc_type = conf.get<std::string>("type");

    auto const opt_script = conf.get_optional<std::string>("tagtransform");
    if (opt_script) {
        new_opts.tag_transform_script =
            adapt_relative_filename(opt_script.get(), options.style);
    }

    new_opts.tag_transform_node_func =
        conf.get_optional<std::string>("tagtransform-node-function");
    new_opts.tag_transform_way_func =
        conf.get_optional<std::string>("tagtransform-way-function");
    new_opts.tag_transform_rel_func =
        conf.get_optional<std::string>("tagtransform-relation-function");
    new_opts.tag_transform_rel_mem_func =
        conf.get_optional<std::string>("tagtransform-relation-member-function");

    new_opts.tblsmain_index = conf.get("tablespace-index", "");
    new_opts.tblsmain_data = conf.get("tablespace-data", "");

    if (conf.get<bool>("enable-hstore", false)) {
        new_opts.hstore_mode = hstore_column::norm;
    }
    override_if<bool>(new_opts.enable_hstore_index, "enable-hstore-index",
                      conf);
    override_if<bool>(new_opts.enable_multi, "enable-multi", conf);
    override_if<bool>(new_opts.hstore_match_only, "hstore-match-only", conf);

    hstores_t hstore_columns;
    boost::optional<const pt::ptree &> hstores =
        conf.get_child_optional("hstores");
    if (hstores) {
        for (pt::ptree::value_type const &val : *hstores) {
            hstore_columns.push_back(val.second.get_value<std::string>());
        }
    }
    new_opts.hstore_columns = hstore_columns;

    std::shared_ptr<geometry_processor> processor =
        geometry_processor::create(proc_type, &new_opts);

    // TODO: we're faking this up, but there has to be a better way?
    osmium::item_type osm_type =
        ((processor->interests() & geometry_processor::interest_node) > 0)
            ? osmium::item_type::node
            : osmium::item_type::way;

    export_list columns;
    pt::ptree const &tags = conf.get_child("tags");
    for (pt::ptree::value_type const &val : tags) {
        pt::ptree const &tag = val.second;
        taginfo info;
        info.name = tag.get<std::string>("name");
        info.type = tag.get<std::string>("type");
        std::string const flags =
            tag.get_optional<std::string>("flags").get_value_or(std::string{});
        // TODO: we fake the line number here - any way to get the right one
        // from the JSON parser?
        info.flags = parse_tag_flags(flags, -1);
        // TODO: shouldn't need to specify a type here?
        columns.add(osm_type, info);
    }

    return std::make_shared<output_multi_t>(name, processor, columns, mid,
                                            new_opts, copy_thread);
}

std::vector<std::shared_ptr<output_t>>
parse_multi_config(std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread)
{
    std::vector<std::shared_ptr<output_t>> outputs;

    if (options.style.empty()) {
        throw std::runtime_error{"Style file is required for `multi' backend, "
                                 "but was not specified."};
    }

    std::string const file_name{options.style};

    try {
        pt::ptree conf;
        pt::read_json(file_name, conf);

        for (pt::ptree::value_type const &val : conf) {
            outputs.push_back(
                parse_multi_single(val.second, mid, options, copy_thread));
        }

    } catch (std::exception const &e) {
        throw std::runtime_error{
            "Unable to parse multi config file `{}': {}."_format(file_name,
                                                                 e.what())};
    }

    return outputs;
}

} // anonymous namespace

std::vector<std::shared_ptr<output_t>>
output_t::create_outputs(std::shared_ptr<middle_query_t> const &mid,
                         options_t const &options)
{
    std::vector<std::shared_ptr<output_t>> outputs;
    auto copy_thread =
        std::make_shared<db_copy_thread_t>(options.database_options.conninfo());

    if (options.output_backend == "pgsql") {
        outputs.push_back(
            std::make_shared<output_pgsql_t>(mid, options, copy_thread));

#ifdef HAVE_LUA
    } else if (options.output_backend == "flex") {
        outputs.push_back(
            std::make_shared<output_flex_t>(mid, options, copy_thread));
#endif

    } else if (options.output_backend == "gazetteer") {
        outputs.push_back(
            std::make_shared<output_gazetteer_t>(mid, options, copy_thread));

    } else if (options.output_backend == "null") {
        outputs.push_back(std::make_shared<output_null_t>(mid, options));

    } else if (options.output_backend == "multi") {
        outputs = parse_multi_config(mid, options, copy_thread);

    } else {
        throw std::runtime_error{
            "Output backend `{}' not recognised. Should be one "
            "of [pgsql, " flex_backend
            "gazetteer, null, multi].\n"_format(options.output_backend)};
    }

    if (outputs.empty()) {
        throw std::runtime_error{"Must have at least one output, "
                                 "but none have been configured."};
    }

    return outputs;
}

output_t::output_t(std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options)
: m_mid(mid), m_options(options)
{}

output_t::~output_t() = default;

options_t const *output_t::get_options() const { return &m_options; }

void output_t::merge_expire_trees(output_t *) {}
