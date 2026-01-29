#ifndef OSM2PGSQL_TEMPLATE_HPP
#define OSM2PGSQL_TEMPLATE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "params.hpp"

#include <fmt/args.h>

#include <string>

class template_t
{
public:
    explicit template_t(std::string_view tmpl) : m_template(tmpl) {}

    void set_params(params_t const &params);

    std::string render() const;

private:
    std::string m_template;
    fmt::dynamic_format_arg_store<fmt::format_context> m_format_store;

}; // class template_t

#endif // OSM2PGSQL_TEMPLATE_HPP
