/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "util.hpp"

#include <iostream>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#elif __has_include(<termios.h>)
#include <termios.h>
#include <unistd.h>
#endif

namespace util {

std::string human_readable_duration(uint64_t seconds)
{
    if (seconds < 60) {
        return fmt::format("{}s", seconds);
    }

    if (seconds < (60 * 60)) {
        return fmt::format("{}s ({}m {}s)", seconds, seconds / 60,
                           seconds % 60);
    }

    auto const secs = seconds % 60;
    auto const mins = seconds / 60;
    return fmt::format("{}s ({}h {}m {}s)", seconds, mins / 60, mins % 60,
                       secs);
}

std::string human_readable_duration(std::chrono::microseconds duration)
{
    return human_readable_duration(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(duration).count()));
}

std::string get_password()
{
#ifdef _WIN32
    HANDLE const handle_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(handle_stdin, &mode);
    SetConsoleMode(handle_stdin, mode & (~ENABLE_ECHO_INPUT));
#elif __has_include(<termios.h>)
    termios orig_flags{};
    tcgetattr(STDIN_FILENO, &orig_flags);
    termios flags = orig_flags;
    flags.c_lflag &= ~static_cast<unsigned int>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &flags);
#endif

    std::string password;
    std::cout << "Password:";
    std::getline(std::cin, password);
    std::cout << "\n";

#ifdef _WIN32
    SetConsoleMode(handle_stdin, mode);
#elif __has_include(<termios.h>)
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_flags);
#endif

    return password;
}

string_joiner_t::string_joiner_t(char delim, char quote, char before,
                                 char after)
: m_delim(delim), m_quote(quote), m_before(before), m_after(after)
{
    if (m_before) {
        m_result += m_before;
    }
}

void string_joiner_t::add(std::string const &item)
{
    if (m_quote) {
        m_result += m_quote;
        m_result += item;
        m_result += m_quote;
    } else {
        m_result += item;
    }
    m_result += m_delim;
}

std::string string_joiner_t::operator()()
{
    if (m_result.size() == 1 && m_before) {
        m_result.clear();
    } else if (m_result.size() > 1) {
        if (m_after) {
            m_result.back() = m_after;
        } else {
            m_result.resize(m_result.size() - 1);
        }
    }
    return std::move(m_result);
}

std::string join(std::vector<std::string> const &items, char delim, char quote,
                 char before, char after)
{
    string_joiner_t joiner{delim, quote, before, after};
    for (auto const &item : items) {
        joiner.add(item);
    }
    return joiner();
}

} // namespace util
