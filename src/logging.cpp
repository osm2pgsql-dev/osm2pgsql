/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"

thread_local unsigned int this_thread_num = 0;

/// Global logger singleton
logger the_logger{};

/// Access the global logger singleton
logger &get_logger() noexcept { return the_logger; }

std::string logger::generate_common_prefix(fmt::text_style const &ts,
                                           char const *prefix)
{
    std::string str;

    if (m_needs_leading_return) {
        m_needs_leading_return = false;
        str += '\n';
    }

    str += fmt::format("{:%Y-%m-%d %H:%M:%S}  ",
                       fmt::localtime(std::time(nullptr)));

    if (m_current_level == log_level::debug) {
        str += fmt::format(ts, "[{}] ", this_thread_num);
    }

    if (prefix) {
        str += fmt::format(ts, "{}: ", prefix);
    }

    return str;
}
