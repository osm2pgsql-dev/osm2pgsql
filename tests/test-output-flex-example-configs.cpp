#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"
#include "format.hpp"

#include <osmium/util/string.hpp>

#include <cstdlib>
#include <string>
#include <vector>

static testing::db::import_t db;

static char const *const data_file = "liechtenstein-2013-08-03.osm.pbf";

std::vector<std::string> get_files() {
    char const *env = std::getenv("EXAMPLE_FILES");
    return osmium::split_string(env, ',', true);
}

TEST_CASE("minimal test for flex example configs")
{
    auto const files = get_files();

    for (auto const& file : files) {
        fmt::print(stderr, "Testing example config '{}.lua'\n", file);
        auto const conf_file = "../../flex-config/" + file + ".lua";
        options_t const options = testing::opt_t().flex(conf_file.c_str());

        REQUIRE_NOTHROW(db.run_file(options, data_file));

        auto conn = db.db().connect();
    }
}
