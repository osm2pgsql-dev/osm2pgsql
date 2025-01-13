/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "template.hpp"

void template_t::set_params(params_t const &params) {
    for (auto const &[key, value] : params) {
        m_format_store.push_back(fmt::arg(key.c_str(), to_string(value)));
    }
}

std::string template_t::render() const
{
    try {
        return fmt::vformat(m_template, m_format_store);
    } catch (fmt::format_error const &) {
        log_error("Missing parameter for template: '{}'", m_template);
        throw;
    }
}
