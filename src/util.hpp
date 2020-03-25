#ifndef OSM2PGSQL_UTIL_HPP
#define OSM2PGSQL_UTIL_HPP

#include "format.hpp"
#include "osmtypes.hpp"

#include <array>
#include <cstdlib>

namespace util {

void exit_nicely();

class integer_to_buffer
{
    // This is enough for 64 bit integers
    static constexpr std::size_t buffer_size = 21;

public:
    template <typename T>
    integer_to_buffer(T value)
    {
        auto const result =
            fmt::format_to_n(m_buffer.begin(), buffer_size - 1, "{}", value);
        assert(result.size < buffer_size);
        *result.out = '\0';
    }

    char const *c_str() const noexcept { return m_buffer.data(); }

private:
    std::array<char, buffer_size> m_buffer;
};

class double_to_buffer
{
    static constexpr std::size_t buffer_size = 32;

public:
    double_to_buffer(double value)
    {
        auto const result =
            fmt::format_to_n(m_buffer.begin(), buffer_size - 1, "{:g}", value);
        assert(result.size < buffer_size);
        *result.out = '\0';
    }

    char const *c_str() const noexcept { return m_buffer.data(); }

private:
    std::array<char, buffer_size> m_buffer;
};

} // namespace util

#endif // OSM2PGSQL_UTIL_HPP
