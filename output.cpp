#include "output.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"

#include <string.h>
#include <stdexcept>
#include <boost/format.hpp>

output_t* output_t::create_output(middle_t* mid, options_t* options)
{
    if (strcmp("pgsql", options->output_backend) == 0) {
        return new output_pgsql_t(mid, options);
    } else if (strcmp("gazetteer", options->output_backend) == 0) {
        return new output_gazetteer_t(mid, options);
    } else if (strcmp("null", options->output_backend) == 0) {
        return new output_null_t(mid, options);
    } else {
        throw std::runtime_error((boost::format("Output backend `%1%' not recognised. Should be one of [pgsql, gazetteer, null].\n") % options->output_backend).str());
    }
}

std::vector<output_t*> output_t::create_outputs(middle_t* mid, options_t* options) {
    std::vector<output_t*> outputs;
    outputs.push_back(create_output(mid, options));
    return outputs;
}

output_t::output_t(middle_query_t* mid_, const options_t* options_): m_mid(mid_), m_options(options_) {

}

output_t::~output_t() {

}

const options_t* output_t::get_options()const {
	return m_options;
}
