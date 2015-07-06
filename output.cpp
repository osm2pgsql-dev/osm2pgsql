#include "output.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"
#include "output-multi.hpp"
#include "taginfo_impl.hpp"

#include <string.h>
#include <stdexcept>

#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
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

boost::shared_ptr<output_t> parse_multi_single(const pt::ptree &conf,
                             const middle_query_t *mid,
                             const options_t &options) {
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
        BOOST_FOREACH(const pt::ptree::value_type &val, *hstores) {
            hstore_columns.push_back(val.second.get_value<std::string>());
        }
    }
    new_opts.hstore_columns = hstore_columns;

    boost::shared_ptr<geometry_processor> processor =
        geometry_processor::create(proc_type, &new_opts);

    // TODO: we're faking this up, but there has to be a better way?
    OsmType osm_type = ((processor->interests() & geometry_processor::interest_node) > 0)
        ? OSMTYPE_NODE : OSMTYPE_WAY;

    export_list columns;
    const pt::ptree &tags = conf.get_child("tags");
    BOOST_FOREACH(const pt::ptree::value_type &val, tags) {
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

    return boost::make_shared<output_multi_t>(name, processor, columns, mid, new_opts);
}

std::vector<boost::shared_ptr<output_t> > parse_multi_config(const middle_query_t *mid, const options_t &options) {
    std::vector<boost::shared_ptr<output_t> > outputs;

    if (!options.style.empty()) {
        const std::string file_name(options.style);

        try {
            pt::ptree conf;
            pt::read_json(file_name, conf);

            BOOST_FOREACH(const pt::ptree::value_type &val, conf) {
                outputs.push_back(parse_multi_single(val.second, mid, options));
            }

        } catch (const std::exception &e) {
            throw std::runtime_error((boost::format("Unable to parse multi config file `%1%': %2%")
                                      % file_name % e.what()).str());
        }
    } else {
        throw std::runtime_error("Style file is required for `multi' backend, but was not specified.");
    }

    return outputs;
}

} // anonymous namespace

std::vector<boost::shared_ptr<output_t> > output_t::create_outputs(const middle_query_t *mid, const options_t &options) {
    std::vector<boost::shared_ptr<output_t> > outputs;

    if (options.output_backend == "pgsql") {
        outputs.push_back(boost::make_shared<output_pgsql_t>(mid, options));

    } else if (options.output_backend == "gazetteer") {
        outputs.push_back(boost::make_shared<output_gazetteer_t>(mid, options));

    } else if (options.output_backend == "null") {
        outputs.push_back(boost::make_shared<output_null_t>(mid, options));

    } else if (options.output_backend == "multi") {
        outputs = parse_multi_config(mid, options);

    } else {
        throw std::runtime_error((boost::format("Output backend `%1%' not recognised. Should be one of [pgsql, gazetteer, null, multi].\n") % options.output_backend).str());
    }

    return outputs;
}

output_t::output_t(const middle_query_t *mid_, const options_t &options_): m_mid(mid_), m_options(options_) {

}

output_t::~output_t() {

}

size_t output_t::pending_count() const{
    return 0;
}

const options_t *output_t::get_options()const {
	return &m_options;
}

void output_t::merge_pending_relations(boost::shared_ptr<output_t> other) {
}
void output_t::merge_expire_trees(boost::shared_ptr<output_t> other) {
}

boost::shared_ptr<id_tracker> output_t::get_pending_relations() {
    return boost::shared_ptr<id_tracker>();
}
boost::shared_ptr<expire_tiles> output_t::get_expire_tree() {
    return boost::shared_ptr<expire_tiles>();
}
