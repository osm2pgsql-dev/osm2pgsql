#include <catch.hpp>

#include <vector>

#include "options.hpp"
#include "taginfo-impl.hpp"
#include "tagtransform.hpp"

char const *TEST_PBF = "foo.pbf";

static void bad_opt(std::vector<char const *> opts, char const *msg)
{
    opts.insert(opts.begin(), "osm2pgsql");
    opts.push_back(TEST_PBF);
    REQUIRE_THROWS_WITH(options_t((int)opts.size(), (char **)&opts[0]),
                        Catch::Matchers::Contains(msg));
}

static options_t opt(std::vector<char const *> opts)
{
    opts.insert(opts.begin(), "osm2pgsql");
    opts.push_back(TEST_PBF);
    return options_t((int)opts.size(), (char **)&opts[0]);
}

TEST_CASE("Insufficient arguments", "[NoDB]")
{
    std::vector<char const *> opts = {"osm2pgsql", "-a", "-c", "--slim"};
    REQUIRE_THROWS_WITH(options_t((int)opts.size(), (char **)&opts[0]),
                        Catch::Matchers::Contains("Usage error"));
}

TEST_CASE("Incompatible arguments", "[NoDB]")
{
    bad_opt({"-a", "-c", "--slim"}, "options can not be used at the same time");

    bad_opt({"--drop"}, "drop only makes sense with");

    bad_opt({"-j", "-k"}, "You can not specify both");

    bad_opt({"-a"}, "--append can only be used with slim mode");
}

TEST_CASE("Middle selection", "[NoDB]")
{
    auto options = opt({"--slim"});
    REQUIRE(options.slim);

    options = opt({});
    REQUIRE_FALSE(options.slim);
}

TEST_CASE("Lua styles", "[NoDB]")
{
#ifdef HAVE_LUA
    auto options = opt({"--tag-transform-script", "non_existing.lua"});
    export_list exlist;
    REQUIRE_THROWS_WITH(tagtransform_t::make_tagtransform(&options, exlist),
                        Catch::Matchers::Contains("No such file or directory"));
#endif
}

TEST_CASE("Parsing tile expiry zoom levels", "[NoDB]")
{
    auto options = opt({"-e", "8-12"});
    CHECK(options.expire_tiles_zoom_min == 8);
    CHECK(options.expire_tiles_zoom == 12);

    options = opt({"-e", "12"});
    CHECK(options.expire_tiles_zoom_min == 12);
    CHECK(options.expire_tiles_zoom == 12);

    //  If very high zoom levels are set, back to still high but valid zoom levels.
    options = opt({"-e", "33-35"});
    CHECK(options.expire_tiles_zoom_min == 31);
    CHECK(options.expire_tiles_zoom == 31);
}

TEST_CASE("Parsing tile expiry zoom levels fails", "[NoDB]")
{
    bad_opt({"-e", "8--12"},
            "Invalid maximum zoom level given for tile expiry");

    bad_opt({"-e", "-8-12"}, "Missing argument for option --expire-tiles. Zoom "
                             "levels must be positive.");

    bad_opt({"-e", "--style", "default.style"},
            "Missing argument for option --expire-tiles. Zoom levels must be "
            "positive.");

    bad_opt({"-e", "a-8"}, "Bad argument for option --expire-tiles. Minimum "
                           "zoom level must be larger than 0.");

    bad_opt({"-e", "6:8"}, "Minimum and maximum zoom level for tile expiry "
                           "must be separated by '-'.");

    bad_opt({"-e", "6-0"}, "Invalid maximum zoom level given for tile expiry.");

    bad_opt({"-e", "6-9a"},
            "Invalid maximum zoom level given for tile expiry.");

    bad_opt({"-e", "0-8"},
            "Bad argument for option --expire-tiles. Minimum zoom level "
            "must be larger than 0.");

    bad_opt({"-e", "6-"}, "Invalid maximum zoom level given for tile expiry.");

    bad_opt({"-e", "-6"},
            "Missing argument for option --expire-tiles. Zoom levels "
            "must be positive.");

    bad_opt({"-e", "0"},
            "Bad argument for option --expire-tiles. Minimum zoom level "
            "must be larger than 0.");
}
