/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "util.hpp"

namespace util {

void string_id_list_t::add(osmid_t id)
{
    fmt::format_to(std::back_inserter(m_list), "{},", id);
}

std::string const &string_id_list_t::get()
{
    assert(!empty());
    m_list.back() = '}';
    return m_list;
}

std::string human_readable_duration(uint64_t seconds)
{
    if (seconds < 60) {
        return "{}s"_format(seconds);
    }

    if (seconds < (60 * 60)) {
        return "{}s ({}m {}s)"_format(seconds, seconds / 60, seconds % 60);
    }

    auto const secs = seconds % 60;
    auto const mins = seconds / 60;
    return "{}s ({}h {}m {}s)"_format(seconds, mins / 60, mins % 60, secs);
}

} // namespace util
