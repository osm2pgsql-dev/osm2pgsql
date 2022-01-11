/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "domain-matcher.hpp"

#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/tag.hpp>

#include <cstring>

static osmium::Tag &fill_buffer(osmium::memory::Buffer &buffer, char const *key,
                                char const *value)
{
    {
        osmium::builder::TagListBuilder builder{buffer};
        builder.add_tag(key, value);
    }
    buffer.commit();

    return *buffer.get<osmium::TagList>(0).begin();
}

TEST_CASE("DomainMatcher: name", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag = fill_buffer(buffer, "bridge:name", "Golden Gate Bridge");
    char const *result = matcher(tag);

    REQUIRE(result);
    REQUIRE(std::strcmp(result, "name") == 0);
}

TEST_CASE("DomainMatcher: name with language", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag =
        fill_buffer(buffer, "bridge:name:en", "The Bridge on the River Kwai");
    char const *result = matcher(tag);

    REQUIRE(result);
    REQUIRE(std::strcmp(result, "name:en") == 0);
}

TEST_CASE("DomainMatcher: no :name", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag = fill_buffer(buffer, "bridge_name", "A Bridge Too Far");
    char const *result = matcher(tag);

    REQUIRE_FALSE(result);
}

TEST_CASE("DomainMatcher: empty matcher", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{""};

    auto const &tag =
        fill_buffer(buffer, "bridge:name", "Tacoma Narrows Bridge");
    char const *result = matcher(tag);

    REQUIRE_FALSE(result);
}

TEST_CASE("DomainMatcher: names", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag =
        fill_buffer(buffer, "bridge:names", "Seven Bridges of Königsberg");
    char const *result = matcher(tag);

    REQUIRE_FALSE(result);
}

TEST_CASE("DomainMatcher: not matching", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag = fill_buffer(buffer, "the_bridge_tag", "Pont du Gard");
    char const *result = matcher(tag);

    REQUIRE_FALSE(result);
}

TEST_CASE("DomainMatcher: empty tag", "[NoDB]")
{
    osmium::memory::Buffer buffer{1024};
    DomainMatcher matcher{"bridge"};

    auto const &tag = fill_buffer(buffer, "", "London Bridge");
    char const *result = matcher(tag);

    REQUIRE_FALSE(result);
}
