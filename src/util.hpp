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

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

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
 * Helper class for timing with a granularity of microseconds. The timer will
 * start on construction or it can be started by calling start(). It is stopped
 * by calling stop(). Timer can be restarted and times will be added up.
 */
class timer_t
{
public:
    timer_t() noexcept : m_start(clock::now()) {}

    void start() noexcept { m_start = clock::now(); }

    /// Stop timer and return elapsed time
    std::chrono::microseconds stop() noexcept
    {
        m_duration += std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now() - m_start);
        return m_duration;
    }

    std::chrono::microseconds elapsed() const noexcept { return m_duration; }

    /**
     * Calculate ratio: value divided by elapsed time.
     *
     * Returns 0 if the elapsed time is 0.
     */
    double per_second(std::size_t value) const noexcept
    {
        auto const seconds =
            std::chrono::duration_cast<std::chrono::seconds>(m_duration)
                .count();
        if (seconds == 0) {
            return 0.0;
        }
        return static_cast<double>(value) / static_cast<double>(seconds);
    }

private:
    using clock = std::chrono::steady_clock;
    std::chrono::time_point<clock> m_start;
    std::chrono::microseconds m_duration{};

}; // class timer_t

std::string human_readable_duration(uint64_t seconds);

std::string human_readable_duration(std::chrono::microseconds duration);

std::string get_password();

/**
 * Helper function that finds items in a container by name. The items must have
 * a name() member function (with a result comparable to std::string) for this
 * to work.
 *
 * \tparam CONTAINER Any kind of container type (must support std::begin/end).
 * \param container The container to look through.
 * \param name The name to look for.
 * \returns Pointer to item, nullptr if not found.
 */
template <typename CONTAINER>
auto find_by_name(CONTAINER &container, std::string const &name)
    -> decltype(&*std::begin(container))
{
    auto const it =
        std::find_if(std::begin(container), std::end(container),
                     [&name](auto const &item) { return item.name() == name; });

    if (it == std::end(container)) {
        return nullptr;
    }

    return &*it;
}

/**
 * Class to help with joining strings.
 *
 * After construction call add() as many times as you want. Call the
 * call operator to get the result, after which the class is unusable.
 *
 * Writes out a string like '("A","B","C")' with "delim" set to the comma,
 * the double quotes as "quote" character and parentheses as "before" and
 * "after" characters. If there are no items (i.e. add() is never called),
 * the result is always empty, so the "before" and "after" characters are
 * not added in this case.
 */
class string_joiner_t
{
public:
    /**
     * Constructor
     * \param delim Char to output between items.
     * \param quote Char to output before and after items (use '\0' for none).
     * \param before Char to output before everything else (use '\0' for none).
     * \param after Char to output after everything else (use '\0' for none).
     */
    explicit string_joiner_t(char delim, char quote = '\0', char before = '\0',
                             char after = '\0');

    /**
     * Add one item to be joined. The quote character is added before and
     * after the item, but the item itself is added as-is without escaping
     * the quote character or anything like that.
     */
    void add(std::string const &item);

    /// Return result (as value!)
    std::string operator()();

private:
    std::string m_result;
    char m_delim;
    char m_quote;
    char m_before;
    char m_after;
};

/**
 * Join all strings in the input vector. This is a convenient wrapper around
 * the string_joiner_t class.
 *
 * \param items The input strings.
 * \param delim Character to output between items.
 * \param quote Character to output before and after items (use '\0' for none).
 * \param before Character to output before everything else (use '\0' for none).
 * \param after Character to output after everything else (use '\0' for none).
 * \returns Result string.
 */
std::string join(std::vector<std::string> const &items, char delim,
                 char quote = '\0', char before = '\0', char after = '\0');

} // namespace util

#endif // OSM2PGSQL_UTIL_HPP
