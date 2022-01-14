#ifndef OSM2PGSQL_UTIL_HPP
#define OSM2PGSQL_UTIL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "osmtypes.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>

namespace util {

class integer_to_buffer
{
    // This is enough for 64 bit integers
    static constexpr std::size_t buffer_size = 21;

public:
    template <typename T>
    explicit integer_to_buffer(T value)
    {
        auto const result =
            fmt::format_to_n(m_buffer.begin(), buffer_size - 1, "{}", value);
        assert(result.size < buffer_size);
        *result.out = '\0';
    }

    char const *c_str() const noexcept { return m_buffer.data(); }

private:
    std::array<char, buffer_size> m_buffer{};
};

class double_to_buffer
{
    static constexpr std::size_t buffer_size = 32;

public:
    explicit double_to_buffer(double value)
    {
        auto const result =
            fmt::format_to_n(m_buffer.begin(), buffer_size - 1, "{:g}", value);
        assert(result.size < buffer_size);
        *result.out = '\0';
    }

    char const *c_str() const noexcept { return m_buffer.data(); }

private:
    std::array<char, buffer_size> m_buffer{};
};

/**
 * Class for building a stringified list of ids in the format "{id1,id2,id3}"
 * for use in PostgreSQL queries.
 */
class string_id_list_t
{
public:
    void add(osmid_t id);

    bool empty() const noexcept { return m_list.size() == 1; }

    std::string const &get();

private:
    std::string m_list{"{"};

}; // class string_id_list_t

/**
 * Helper class for timing with a granularity of seconds. The timer will
 * start on construction and is stopped by calling stop().
 */
class timer_t
{
public:
    timer_t() noexcept : m_start(std::time(nullptr)) {}

    /// Stop timer and return elapsed time
    uint64_t stop() noexcept
    {
        m_stop = std::time(nullptr);
        return static_cast<uint64_t>(m_stop - m_start);
    }

    /// Return elapsed time
    uint64_t elapsed() const noexcept
    {
        return static_cast<uint64_t>(m_stop - m_start);
    }

    /**
     * Calculate ratio: value divided by elapsed time.
     *
     * Returns 0 if the elapsed time is 0.
     */
    double per_second(double value) const noexcept
    {
        auto const seconds = elapsed();
        if (seconds == 0) {
            return 0.0;
        }
        return value / static_cast<double>(seconds);
    }

private:
    std::time_t m_start;
    std::time_t m_stop = 0;

}; // class timer_t

std::string human_readable_duration(uint64_t seconds);

std::string human_readable_duration(std::chrono::milliseconds ms);

std::string get_password();

} // namespace util

#endif // OSM2PGSQL_UTIL_HPP
