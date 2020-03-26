#include "config.h"
#include "format.hpp"
#include "options.hpp"
#include "tagtransform-c.hpp"
#include "tagtransform.hpp"

#ifdef HAVE_LUA
#include "tagtransform-lua.hpp"
#endif

std::unique_ptr<tagtransform_t>
tagtransform_t::make_tagtransform(options_t const *options,
                                  export_list const &exlist)
{
    if (options->tag_transform_script) {
#ifdef HAVE_LUA
        fmt::print(stderr,
                   "Using lua based tag processing pipeline with script {}\n",
                   options->tag_transform_script.get());
        return std::unique_ptr<tagtransform_t>(new lua_tagtransform_t(options));
#else
        throw std::runtime_error("Error: Could not init lua tag transform, as "
                                 "lua support was not compiled into this "
                                 "version");
#endif
    }

    fmt::print(stderr, "Using built-in tag processing pipeline\n");
    return std::unique_ptr<tagtransform_t>(
        new c_tagtransform_t(options, exlist));
}

tagtransform_t::~tagtransform_t() = default;
