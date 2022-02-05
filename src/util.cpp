/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
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

std::string human_readable_duration(std::chrono::milliseconds ms)
{
    return human_readable_duration(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(ms).count()));
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

} // namespace util
