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

TEST_CASE("human readable time durations")
{
    REQUIRE(util::human_readable_duration(0) == "0s");
    REQUIRE(util::human_readable_duration(17) == "17s");
    REQUIRE(util::human_readable_duration(59) == "59s");
    REQUIRE(util::human_readable_duration(60) == "60s (1m 0s)");
    REQUIRE(util::human_readable_duration(66) == "66s (1m 6s)");
    REQUIRE(util::human_readable_duration(247) == "247s (4m 7s)");
    REQUIRE(util::human_readable_duration(3599) == "3599s (59m 59s)");
    REQUIRE(util::human_readable_duration(3600) == "3600s (1h 0m 0s)");
    REQUIRE(util::human_readable_duration(3723) == "3723s (1h 2m 3s)");
    REQUIRE(util::human_readable_duration(152592) == "152592s (42h 23m 12s)");
}

