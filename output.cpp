#include "output.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"

#include <string.h>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

namespace {

output_t *parse_multi_single(const pt::ptree &conf,
                             const middle_query_t *mid,
                             const options_t *options) {
    std::string name = conf.get<std::string>("name");

}

std::vector<output_t*> parse_multi_config(const middle_query_t *mid, const options_t *options) {
    std::vector<output_t*> outputs;

    if ((options->style != NULL) && (strlen(options->style) > 0)) {
        const std::string file_name(options->style);

        try {
            pt::ptree conf;
            pt::read_json(file_name, conf);

            BOOST_FOREACH(const pt::ptree::value_type &val, conf) {
                outputs.push_back(parse_multi_single(val.second, mid, options));
            }

        } catch (const std::exception &e) {
            // free up any allocated resources
            BOOST_FOREACH(output_t *out, outputs) { delete out; }

            throw std::runtime_error((boost::format("Unable to parse multi config file `%1%': %2%")
                                      % file_name % e.what()).str());
        }
    } else {
        throw std::runtime_error("Style file is required for `multi' backend, but was not specified.");
    }

    return outputs;
}

} // anonymous namespace

output_t* output_t::create_output(const middle_query_t *mid, const options_t* options) {
    std::vector<output_t *> outputs = create_outputs(mid, options);
    
    if (outputs.empty()) {
        throw std::runtime_error("Unable to construct output backend.");
    }

    output_t *output = outputs.front();
    for (size_t i = 1; i < outputs.size(); ++i) {
        delete outputs[i];
    }

    return output;
}

std::vector<output_t*> output_t::create_outputs(const middle_query_t *mid, const options_t* options) {
    std::vector<output_t*> outputs;

    if (strcmp("pgsql", options->output_backend) == 0) {
        outputs.push_back(new output_pgsql_t(mid, options));

    } else if (strcmp("gazetteer", options->output_backend) == 0) {
        outputs.push_back(new output_gazetteer_t(mid, options));

    } else if (strcmp("null", options->output_backend) == 0) {
        outputs.push_back(new output_null_t(mid, options));

    } else if (strcmp("multi", options->output_backend) == 0) {
        outputs = parse_multi_config(mid, options);

    } else {
        throw std::runtime_error((boost::format("Output backend `%1%' not recognised. Should be one of [pgsql, gazetteer, null, multi].\n") % options->output_backend).str());
    }

    return outputs;
}

output_t::output_t(const middle_query_t *mid_, const options_t* options_): m_mid(mid_), m_options(options_) {

}

output_t::~output_t() {

}

const options_t* output_t::get_options()const {
	return m_options;
}
