#ifndef OSM2PGSQL_IDLIST_HPP
#define OSM2PGSQL_IDLIST_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * This file contains the definition of the idlist_t class.
 */

#include "osmtypes.hpp"

#include <vector>

struct idlist_t : public std::vector<osmid_t>
{
    // Get all constructors from std::vector
    using vector<osmid_t>::vector;

    // Even though we got all constructors from std::vector we need this on
    // some compilers/libraries for some reason.
    idlist_t() = default;

};

#endif // OSM2PGSQL_IDLIST_HPP
