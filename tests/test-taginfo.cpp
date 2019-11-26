
#include <catch.hpp>

#include "taginfo-impl.hpp"

TEST_CASE("parse_tag_flags")
{
    CHECK(parse_tag_flags("", 0) == 0);
    CHECK(parse_tag_flags("polygon", 0) == FLAG_POLYGON);
    CHECK(parse_tag_flags("linear", 0) == FLAG_LINEAR);
    CHECK(parse_tag_flags("nocolumn", 0) == FLAG_NOCOLUMN);
    CHECK(parse_tag_flags("phstore", 0) == FLAG_PHSTORE);
    CHECK(parse_tag_flags("delete", 0) == FLAG_DELETE);
    CHECK(parse_tag_flags("nocache", 0) == FLAG_NOCACHE);
    CHECK(parse_tag_flags("UNKNOWN", 0) == 0);
    CHECK(parse_tag_flags("polygon,phstore", 0) == (FLAG_POLYGON | FLAG_PHSTORE));
    CHECK(parse_tag_flags("polygon\nnocache", 0) == (FLAG_POLYGON | FLAG_NOCACHE));
    CHECK(parse_tag_flags("polygon\nnocache,delete", 0) == (FLAG_POLYGON | FLAG_NOCACHE | FLAG_DELETE));
    CHECK(parse_tag_flags("polygon, nocache,delete", 0) == (FLAG_POLYGON | FLAG_DELETE));
}

