#include "middle.hpp"

middle_t::middle_t(output_t* out_):out(out_) {

}

middle_t::~middle_t() {
}

slim_middle_t::slim_middle_t(output_t* out_):middle_t(out_) {

}

slim_middle_t::~slim_middle_t() {
}

middle_t::way_cb_func::~way_cb_func() {
}

middle_t::rel_cb_func::~rel_cb_func() {
}

