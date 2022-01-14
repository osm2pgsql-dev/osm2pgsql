/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "middle.hpp"
#include "options.hpp"

std::shared_ptr<middle_t>
create_middle(std::shared_ptr<thread_pool_t> thread_pool,
              options_t const &options)
{
    if (options.slim) {
        return std::make_shared<middle_pgsql_t>(std::move(thread_pool),
                                                &options);
    }

    return std::make_shared<middle_ram_t>(std::move(thread_pool), &options);
}

