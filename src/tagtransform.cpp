/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tagtransform.hpp"
#include "config.h"
#include "logging.hpp"
#include "options.hpp"
#include "tagtransform-c.hpp"

#ifdef HAVE_LUA
#include "tagtransform-lua.hpp"
#endif

std::unique_ptr<tagtransform_t>
tagtransform_t::make_tagtransform(options_t const *options,
                                  export_list const &exlist)
{
    if (options->tag_transform_script) {
#ifdef HAVE_LUA
        log_debug("Using lua based tag transformations with script {}",
                  options->tag_transform_script.get());
        return std::unique_ptr<tagtransform_t>(new lua_tagtransform_t{options});
#else
        throw std::runtime_error{"Error: Could not init lua tag transform, as "
                                 "lua support was not compiled into this "
                                 "version."};
#endif
    }

    log_debug("Using built-in tag transformations");
    return std::unique_ptr<tagtransform_t>(
        new c_tagtransform_t{options, exlist});
}

tagtransform_t::~tagtransform_t() = default;
