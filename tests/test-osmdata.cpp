#include <catch.hpp>

#include "osmdata.hpp"

TEST_CASE("it's good if input data is ordered")
{
    type_id_version const tiv1{osmium::item_type::node, 1, 1};
    type_id_version const tiv2{osmium::item_type::node, 1, 2};
    type_id_version const tiv3{osmium::item_type::node, 2, 1};
    type_id_version const tiv4{osmium::item_type::way, 1, 1};
    type_id_version const tiv5{osmium::item_type::way, 2, 1};
    type_id_version const tiv6{osmium::item_type::relation, 1, 1};
    type_id_version const tiv7{osmium::item_type::relation, 1, 2};

    REQUIRE_NOTHROW(check_input(tiv1, tiv2));
    REQUIRE_NOTHROW(check_input(tiv2, tiv3));
    REQUIRE_NOTHROW(check_input(tiv3, tiv4));
    REQUIRE_NOTHROW(check_input(tiv4, tiv5));
    REQUIRE_NOTHROW(check_input(tiv5, tiv6));
    REQUIRE_NOTHROW(check_input(tiv6, tiv7));
}

TEST_CASE("negative OSM object ids are not allowed")
{
    type_id_version const tivn{osmium::item_type::node, -17, 1};
    type_id_version const tivw{osmium::item_type::way, -1, 1};
    type_id_version const tivr{osmium::item_type::relation, -999, 17};

    REQUIRE_THROWS_WITH(
        check_input(tivn, tivn),
        "Negative OSM object ids are not allowed: node id -17.");
    REQUIRE_THROWS_WITH(check_input(tivw, tivw),
                        "Negative OSM object ids are not allowed: way id -1.");
    REQUIRE_THROWS_WITH(
        check_input(tivr, tivr),
        "Negative OSM object ids are not allowed: relation id -999.");
}

TEST_CASE("objects of the same type must be ordered")
{
    type_id_version const tiv1{osmium::item_type::node, 42, 1};
    type_id_version const tiv2{osmium::item_type::node, 3, 1};

    REQUIRE_THROWS_WITH(check_input(tiv1, tiv2),
                        "Input data is not ordered: node id 3 after 42.");
}

TEST_CASE("a node after a way or relation is not allowed")
{
    type_id_version const tiv1w{osmium::item_type::way, 42, 1};
    type_id_version const tiv1r{osmium::item_type::relation, 42, 1};
    type_id_version const tiv2{osmium::item_type::node, 100, 1};

    REQUIRE_THROWS_WITH(check_input(tiv1w, tiv2),
                        "Input data is not ordered: node after way.");
    REQUIRE_THROWS_WITH(check_input(tiv1r, tiv2),
                        "Input data is not ordered: node after relation.");
}

TEST_CASE("a way after a relation is not allowed")
{
    type_id_version const tiv1{osmium::item_type::relation, 42, 1};
    type_id_version const tiv2{osmium::item_type::way, 100, 1};

    REQUIRE_THROWS_WITH(check_input(tiv1, tiv2),
                        "Input data is not ordered: way after relation.");
}

TEST_CASE("versions must be ordered")
{
    type_id_version const tiv1{osmium::item_type::way, 42, 2};
    type_id_version const tiv2{osmium::item_type::way, 42, 1};

    REQUIRE_THROWS_WITH(
        check_input(tiv1, tiv2),
        "Input data is not ordered: way id 42 version 1 after 2.");
}

