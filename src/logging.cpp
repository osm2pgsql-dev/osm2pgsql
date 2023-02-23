/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"

#include <osmium/thread/util.hpp>

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

void logger::init_thread(unsigned int num) const
{
    // Store thread number in thread local variable
    this_thread_num = num;

    // Set thread name in operating system.
    // On Linux thread names have a maximum length of 16 characters.
    std::string name{"_osm2pgsql_"};
    name.append(std::to_string(num));
    osmium::thread::set_thread_name(name.c_str());
}
