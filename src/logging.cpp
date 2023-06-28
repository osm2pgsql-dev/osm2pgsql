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

void logger::generate_common_prefix(std::string *str, fmt::text_style const &ts,
                                    char const *prefix) const
{
    *str += fmt::format("{:%Y-%m-%d %H:%M:%S}  ",
                       fmt::localtime(std::time(nullptr)));

    if (m_current_level == log_level::debug) {
        *str += fmt::format(ts, "[{:02d}] ", this_thread_num);
    }

    if (prefix) {
        *str += fmt::format(ts, "{}: ", prefix);
    }
}

void logger::init_thread(unsigned int num)
{
    // Store thread number in thread local variable
    this_thread_num = num;

    // Set thread name in operating system.
    // On Linux thread names have a maximum length of 16 characters.
    std::string name{"_osm2pgsql_"};
    name.append(std::to_string(num));
    osmium::thread::set_thread_name(name.c_str());
}
