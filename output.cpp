#include "output.hpp"
#include "db-copy.hpp"
#include "output-gazetteer.hpp"
#include "output-multi.hpp"
#include "output-null.hpp"
#include "output-pgsql.hpp"
#include "taginfo_impl.hpp"

#include <cstring>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

namespace {

template <typename T>
void override_if(T &t, const std::string &key, const pt::ptree &conf) {
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

    std::string name = conf.get<std::string>("name");
    std::string proc_type = conf.get<std::string>("type");

    new_opts.tag_transform_script = conf.get_optional<std::string>("tagtransform");

    new_opts.tag_transform_node_func = conf.get_optional<std::string>("tagtransform-node-function");
    new_opts.tag_transform_way_func = conf.get_optional<std::string>("tagtransform-way-function");
    new_opts.tag_transform_rel_func = conf.get_optional<std::string>("tagtransform-relation-function");
    new_opts.tag_transform_rel_mem_func = conf.get_optional<std::string>("tagtransform-relation-member-function");

    new_opts.tblsmain_index = conf.get_optional<std::string>("tablespace-index");
    new_opts.tblsmain_data = conf.get_optional<std::string>("tablespace-data");
    override_if<int>(new_opts.hstore_mode, "enable-hstore", conf);
    override_if<bool>(new_opts.enable_hstore_index, "enable-hstore-index", conf);
    override_if<bool>(new_opts.enable_multi, "enable-multi", conf);
    override_if<bool>(new_opts.hstore_match_only, "hstore-match-only", conf);

    hstores_t hstore_columns;
    boost::optional<const pt::ptree &> hstores = conf.get_child_optional("hstores");
    if (hstores) {
        for (const pt::ptree::value_type &val: *hstores) {
            hstore_columns.push_back(val.second.get_value<std::string>());
        }
    }
    new_opts.hstore_columns = hstore_columns;

    std::shared_ptr<geometry_processor> processor =
        geometry_processor::create(proc_type, &new_opts);

    // TODO: we're faking this up, but there has to be a better way?
    osmium::item_type osm_type = ((processor->interests() & geometry_processor::interest_node) > 0)
        ? osmium::item_type::node : osmium::item_type::way;

    export_list columns;
    const pt::ptree &tags = conf.get_child("tags");
    for (const pt::ptree::value_type &val: tags) {
        const pt::ptree &tag = val.second;
        taginfo info;
        info.name = tag.get<std::string>("name");
        info.type = tag.get<std::string>("type");
        std::string flags = tag.get_optional<std::string>("flags").get_value_or(std::string());
        // TODO: we fake the line number here - any way to get the right one
        // from the JSON parser?
        info.flags = parse_tag_flags(flags.c_str(), -1);
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
    std::vector<std::shared_ptr<output_t> > outputs;

    if (options.style.empty()) {
        throw std::runtime_error("Style file is required for `multi' backend, "
                                 "but was not specified.");
    }

    const std::string file_name(options.style);

    try {
        pt::ptree conf;
        pt::read_json(file_name, conf);

        for (const pt::ptree::value_type &val : conf) {
            outputs.push_back(
                parse_multi_single(val.second, mid, options, copy_thread));
        }

    } catch (const std::exception &e) {
        throw std::runtime_error(
            (boost::format("Unable to parse multi config file `%1%': %2%") %
             file_name % e.what())
                .str());
    }

    return outputs;
}

} // anonymous namespace

std::vector<std::shared_ptr<output_t>>
output_t::create_outputs(std::shared_ptr<middle_query_t> const &mid,
                         options_t const &options)
{
    std::vector<std::shared_ptr<output_t> > outputs;
    auto copy_thread =
        std::make_shared<db_copy_thread_t>(options.database_options.conninfo());

    if (options.output_backend == "pgsql") {
        outputs.push_back(
            std::make_shared<output_pgsql_t>(mid, options, copy_thread));

    } else if (options.output_backend == "gazetteer") {
        outputs.push_back(
            std::make_shared<output_gazetteer_t>(mid, options, copy_thread));

    } else if (options.output_backend == "null") {
        outputs.push_back(std::make_shared<output_null_t>(mid, options));

    } else if (options.output_backend == "multi") {
        outputs = parse_multi_config(mid, options, copy_thread);

    } else {
        throw std::runtime_error((boost::format("Output backend `%1%' not recognised. Should be one of [pgsql, gazetteer, null, multi].\n") % options.output_backend).str());
    }

    return outputs;
}

output_t::output_t(std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options)
: m_mid(mid), m_options(options)
{}

output_t::~output_t() = default;

size_t output_t::pending_count() const
{
    return 0;
}

const options_t *output_t::get_options() const
{
    return &m_options;
}

void output_t::merge_pending_relations(output_t*) {}

void output_t::merge_expire_trees(output_t*) {}

