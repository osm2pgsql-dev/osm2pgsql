#include <catch.hpp>

#include "util.hpp"

#include <cstring>
#include <limits>

TEST_CASE("integer_to_buffer 1")
{
    util::integer_to_buffer buffer{1};
    REQUIRE(std::strcmp(buffer.c_str(), "1") == 0);
}

TEST_CASE("integer_to_buffer max")
{
    util::integer_to_buffer buffer{std::numeric_limits<osmid_t>::max()};
    REQUIRE(std::strcmp(buffer.c_str(), "9223372036854775807") == 0);
}

TEST_CASE("integer_to_buffer min")
{
    util::integer_to_buffer buffer{std::numeric_limits<osmid_t>::min()};
    REQUIRE(std::strcmp(buffer.c_str(), "-9223372036854775808") == 0);
}

TEST_CASE("double_to_buffer 0")
{
    util::double_to_buffer buffer{0.0};
    REQUIRE(std::strcmp(buffer.c_str(), "0") == 0);
}

TEST_CASE("double_to_buffer 3.141")
{
    util::double_to_buffer buffer{3.141};
    REQUIRE(std::strcmp(buffer.c_str(), "3.141") == 0);
}
