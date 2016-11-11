/*
 * Test wildcard matching function.
 */

#include <iostream>
#include <string>
#include <vector>

#include "wildcmp.hpp"

struct test_t {
    std::string wildcard;
    std::string match;
    bool result;

    test_t(const std::string& w, const std::string& m, bool r)
    : wildcard(w), match(m), result(r) {}
};

static std::vector<test_t> tests {
    {"fhwieurwe", "fhwieurwe", true},
    {"fhwieurwe", "fhwieurw", false},
    {"fhwieurw", "fhwieurwe", false},
    {"*", "foo", true},
    {"r*", "foo", false},
    {"r*", "roo", true},
    {"*bar", "Hausbar", true},
    {"*bar", "Haustar", false},
    {"*", "", true},
    {"kin*la", "kinla", true},
    {"kin*la", "kinLLla", true},
    {"kin*la", "kinlalalala", true},
    {"kin*la", "kinlaa", false},
    {"kin*la", "ki??laa", false},
    {"1*2*3", "123", true},
    {"1*2*3", "1xX23", true},
    {"1*2*3", "12y23", true},
    {"1*2*3", "12", false},
    {"bo??f", "boxxf", true},
    {"bo??f", "boxf", false},
    {"?5?", "?5?", true},
    {"?5?", "x5x", true},
};

int main()
{
    int ret = 0;

    for (const auto& test: tests) {
        if (bool(wildMatch(test.wildcard.c_str(), test.match.c_str())) != test.result) {
            std::cerr << "Wildcard match failed:";
            std::cerr << "\n  expression: " << test.wildcard;
            std::cerr << "\n  test string: " << test.match;
            std::cerr << "\n  expected: " << test.result;
            std::cerr << "\n  got: " << (!test.result) << "\n";
            ret = 1;
        }
    }

    return ret;
}
