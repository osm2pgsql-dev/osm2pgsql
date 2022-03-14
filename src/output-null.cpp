/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "output-null.hpp"

std::shared_ptr<output_t>
output_null_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &) const
{
    return std::make_shared<output_null_t>(mid, m_thread_pool, *get_options());
}

output_null_t::output_null_t(std::shared_ptr<middle_query_t> const &mid,
                             std::shared_ptr<thread_pool_t> thread_pool,
                             options_t const &options)
: output_t(mid, std::move(thread_pool), options)
{}

output_null_t::~output_null_t() = default;
