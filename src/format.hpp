#ifndef OSM2PGSQL_FORMAT_HPP
#define OSM2PGSQL_FORMAT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include <stdexcept>

template <typename S, typename... TArgs>
std::runtime_error fmt_error(S const &format_str, TArgs &&...args)
{
    return std::runtime_error{
        fmt::format(format_str, std::forward<TArgs>(args)...)};
}

#endif // OSM2PGSQL_FORMAT_HPP
