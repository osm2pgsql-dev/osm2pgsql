#ifndef OSM2PGSQL_OVERLOADED_HPP
#define OSM2PGSQL_OVERLOADED_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

// This magic is used for visiting geometries. For an explanation see for
// instance here:
// https://arne-mertz.de/2018/05/overload-build-a-variant-visitor-on-the-fly/
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

#endif // OSM2PGSQL_OVERLOADED_HPP
