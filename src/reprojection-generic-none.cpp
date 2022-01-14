/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <stdexcept>

#include "reprojection.hpp"

std::shared_ptr<reprojection> reprojection::make_generic_projection(int)
{
    throw std::runtime_error{"No generic projection library available."};
}

std::string get_proj_version() { return "[disabled]"; }

