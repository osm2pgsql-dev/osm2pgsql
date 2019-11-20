#include <catch.hpp>

#include "wildcmp.hpp"

TEST_CASE("Wildcard matching", "[NoDB]")
{
    CHECK(wildMatch("fhwieurwe", "fhwieurwe"));
    CHECK_FALSE(wildMatch("fhwieurwe", "fhwieurw"));
    CHECK_FALSE(wildMatch("fhwieurw", "fhwieurwe"));
    CHECK(wildMatch("*", "foo"));
    CHECK_FALSE(wildMatch("r*", "foo"));
    CHECK(wildMatch("r*", "roo"));
    CHECK(wildMatch("*bar", "Hausbar"));
    CHECK_FALSE(wildMatch("*bar", "Haustar"));
    CHECK(wildMatch("*", ""));
    CHECK(wildMatch("kin*la", "kinla"));
    CHECK(wildMatch("kin*la", "kinLLla"));
    CHECK(wildMatch("kin*la", "kinlalalala"));
    CHECK_FALSE(wildMatch("kin*la", "kinlaa"));
    CHECK_FALSE(wildMatch("kin*la", "ki??laa"));
    CHECK(wildMatch("1*2*3", "123"));
    CHECK(wildMatch("1*2*3", "1xX23"));
    CHECK(wildMatch("1*2*3", "12y23"));
    CHECK_FALSE(wildMatch("1*2*3", "12"));
    CHECK(wildMatch("bo??f", "boxxf"));
    CHECK_FALSE(wildMatch("bo??f", "boxf"));
    CHECK(wildMatch("?5?", "?5?"));
    CHECK(wildMatch("?5?", "x5x"));
}
