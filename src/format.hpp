#ifndef OSM2PGSQL_FORMAT_HPP
#define OSM2PGSQL_FORMAT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#define FMT_HEADER_ONLY
#include <fmt/format.h>

inline auto operator"" _format(const char *s, std::size_t n)
{
    return [=](auto &&...args) {
#if FMT_VERSION < 80100
        return fmt::format(std::string_view{s, n}, args...);
#else
        return fmt::format(fmt::runtime(std::string_view{s, n}), args...);
#endif
    };
}

#endif // OSM2PGSQL_FORMAT_HPP
