/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "output.hpp"

#include "db-copy.hpp"
#include "format.hpp"
#include "options.hpp"
#include "output-flex.hpp"
#include "output-null.hpp"
#include "output-pgsql.hpp"

#include <stdexcept>
#include <string>
#include <utility>

std::shared_ptr<output_t>
output_t::create_output(std::shared_ptr<middle_query_t> const &mid,
                        std::shared_ptr<thread_pool_t> thread_pool,
                        options_t const &options,
                        properties_t const &properties)
{
    if (options.output_backend == "pgsql") {
        return std::make_shared<output_pgsql_t>(mid, std::move(thread_pool),
                                                options);
    }

    if (options.output_backend == "flex") {
        return std::make_shared<output_flex_t>(mid, std::move(thread_pool),
                                               options, properties);
    }

    if (options.output_backend == "null") {
        return std::make_shared<output_null_t>(mid, std::move(thread_pool),
                                               options);
    }

    throw fmt_error("Output backend '{}' not recognised. Should be one of"
                    " [pgsql, flex, null].",
                    options.output_backend);
}

output_t::output_t(std::shared_ptr<middle_query_t> mid,
                   std::shared_ptr<thread_pool_t> thread_pool,
                   options_t const &options)
: m_mid(std::move(mid)), m_options(&options),
  m_thread_pool(std::move(thread_pool))
{}

output_t::output_t(output_t const *other, std::shared_ptr<middle_query_t> mid)
: m_mid(std::move(mid)), m_options(other->m_options),
  m_thread_pool(other->m_thread_pool),
  m_output_requirements(other->m_output_requirements)
{}

output_t::~output_t() = default;

void output_t::free_middle_references() { m_mid.reset(); }
