#ifndef OSM2PGSQL_LOGGING_HPP
#define OSM2PGSQL_LOGGING_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"

#include <osmium/util/file.hpp>

#include <fmt/chrono.h>
#include <fmt/color.h>

#include <cstdio>
#include <utility>

enum class log_level
{
    debug = 1,
    info = 2,
    warn = 3,
    error = 4
};

/**
 * This class contains the logging state and code. It is intended as a
 * singleton class. Its use is mostly wrapped in the log_*() free functions.
 */
class logger
{
public:
    template <typename S, typename... TArgs>
    void log(log_level with_level, char const *prefix,
             fmt::text_style const &style, S const &format_str, TArgs &&...args)
    {
        if (with_level < m_current_level) {
            return;
        }

        auto const &ts = m_use_color ? style : fmt::text_style{};

        auto str = generate_common_prefix(ts, prefix);

        str += fmt::format(ts, format_str, std::forward<TArgs>(args)...);
        str += '\n';

        if (std::fputs(str.c_str(), stderr) < 0) {
            throw std::runtime_error{"Can not write to log"};
        }
    }

    bool log_sql() const noexcept { return m_log_sql; }

    bool log_sql_data() const noexcept { return m_log_sql_data; }

    void set_level(log_level level) noexcept { m_current_level = level; }

    bool debug_enabled() const noexcept
    {
        return m_current_level == log_level::debug;
    }

    void enable_sql() noexcept { m_log_sql = true; }

    void enable_sql_data() noexcept { m_log_sql_data = true; }

    bool show_progress() const noexcept { return m_show_progress; }

    void enable_progress() noexcept { m_show_progress = true; }
    void disable_progress() noexcept { m_show_progress = false; }
    void auto_progress() noexcept { m_show_progress = osmium::util::isatty(2); }

    void needs_leading_return() noexcept { m_needs_leading_return = true; }
    void no_leading_return() noexcept { m_needs_leading_return = false; }

    void init_thread(unsigned int num) const;

private:
    std::string generate_common_prefix(fmt::text_style const &ts,
                                       char const *prefix);

    log_level m_current_level = log_level::info;
    bool m_log_sql = false;
    bool m_log_sql_data = false;
    bool m_show_progress = true;
    bool m_needs_leading_return = false;

#ifdef _WIN32
    bool m_use_color = false;
#else
    bool m_use_color = osmium::util::isatty(2);
#endif

}; // class logger

logger &get_logger() noexcept;

template <typename S, typename... TArgs>
void log_debug(S const &format_str, TArgs &&... args)
{
    get_logger().log(log_level::debug, nullptr, {}, format_str,
                     std::forward<TArgs>(args)...);
}

template <typename S, typename... TArgs>
void log_info(S const &format_str, TArgs &&... args)
{
    get_logger().log(log_level::info, nullptr, {}, format_str,
                     std::forward<TArgs>(args)...);
}

template <typename S, typename... TArgs>
void log_warn(S const &format_str, TArgs &&... args)
{
    get_logger().log(log_level::warn, "WARNING", fg(fmt::color::red),
                     format_str, std::forward<TArgs>(args)...);
}

template <typename S, typename... TArgs>
void log_error(S const &format_str, TArgs &&... args)
{
    get_logger().log(log_level::error, "ERROR",
                     fmt::emphasis::bold | fg(fmt::color::red), format_str,
                     std::forward<TArgs>(args)...);
}

template <typename S, typename... TArgs>
void log_sql(S const &format_str, TArgs &&... args)
{
    auto &logger = get_logger();
    if (logger.log_sql()) {
        logger.log(log_level::error, "SQL", fg(fmt::color::blue), format_str,
                   std::forward<TArgs>(args)...);
    }
}

template <typename S, typename... TArgs>
void log_sql_data(S const &format_str, TArgs &&... args)
{
    auto &logger = get_logger();
    if (logger.log_sql_data()) {
        logger.log(log_level::error, "SQL", {}, format_str,
                   std::forward<TArgs>(args)...);
    }
}

#endif // OSM2PGSQL_LOGGING_HPP
