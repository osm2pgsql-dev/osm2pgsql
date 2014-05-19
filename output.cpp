#include "output.hpp"

output_t::output_t(middle_t* mid_, const output_options* options_): m_mid(mid_), m_options(options_) {

}

output_t::output_t() {

}

output_t::~output_t() {

}

const output_options* output_t::get_options()const {
	return m_options;
}
