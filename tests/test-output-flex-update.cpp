/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"
#include "common-pg.hpp"

namespace {

testing::db::import_t db;

char const *const CONF_FILE = "test_output_flex.lua";

// Return a string with the schema name prepended to the table name.
std::string with_schema(char const *table_name, options_t const &options)
{
    if (options.dbschema.empty()) {
        return {table_name};
    }
    return options.dbschema + "." + table_name;
}

} // anonymous namespace

struct options_slim_default
{
    static options_t options()
    {
        return testing::opt_t().slim().flex(CONF_FILE);
    }
};

struct options_slim_expire
{
    static options_t options()
    {
        options_t o = options_slim_default::options();
        o.expire_tiles_zoom = 10;
        return o;
    }
};

struct options_slim_schema
{
    static options_t options()
    {
        auto conn = db.db().connect();
        // Create limited user (if it doesn't exist yet),
        // which we need to test that the public schema won't be touched.
        // If the public schema is tried to be modified at any point, this user won't have the
        // necessary permissions, and hence the test will fail.
        conn.exec(R"(
DO
$$
BEGIN
   IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'limited') THEN
      CREATE ROLE limited LOGIN PASSWORD 'password_limited';
   END IF;
END
$$;
                  )");
        conn.exec("REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA public FROM "
                  "PUBLIC, limited;");
        conn.exec("REVOKE CREATE ON SCHEMA public FROM PUBLIC, limited;");
        conn.exec(
            "CREATE SCHEMA IF NOT EXISTS myschema AUTHORIZATION limited;");
        conn.close();
        return testing::opt_t()
            .slim()
            .flex(CONF_FILE)
            .schema("myschema")
            .user("limited", "password_limited");
    }
};

TEMPLATE_TEST_CASE("updating a node", "", options_slim_default,
                   options_slim_expire, options_slim_schema)
{
    options_t options = TestType::options();

    // import a node...
    REQUIRE_NOTHROW(db.run_import(options, "n10 v1 dV x10 y10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));

    // give the node a tag...
    options.append = true;
    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v2 dV x10 y10 Tamenity=restaurant\n"));

    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_point", options),
                           "node_id = 10 AND tags->'amenity' = 'restaurant'"));

    SECTION("remove the tag from node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v3 dV x10 y10\n"));
    }

    SECTION("delete the node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v3 dD\n"));
    }

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
}

TEMPLATE_TEST_CASE("updating a way", "", options_slim_default,
                   options_slim_expire, options_slim_schema)
{
    options_t options = TestType::options();

    // import a simple way...
    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.1\n"
                                  "n11 v1 dV x10.1 y10.2\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options),
                                "osm_id = 20 AND tags->'highway' = 'primary' "
                                "AND ST_NumPoints(geom) = 2"));

    // now change the way itself...
    options.append = true;
    REQUIRE_NOTHROW(
        db.run_import(options, "w20 v2 dV Thighway=secondary Nn10,n11\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options),
                                "osm_id = 20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now change a node in the way...
    REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x10.0 y10.3\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options),
                                "osm_id = 20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now add a node to the way...
    REQUIRE_NOTHROW(db.run_import(
        options, "n12 v1 dV x10.2 y10.1\n"
                 "w20 v3 dV Thighway=residential Nn10,n11,n12\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options),
                                "osm_id = 20 AND tags->'highway' = "
                                "'residential' AND ST_NumPoints(geom) = 3"));

    // now delete the way...
    REQUIRE_NOTHROW(db.run_import(options, "w20 v4 dD\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
}

TEMPLATE_TEST_CASE("ways as linestrings and polygons", "", options_slim_default,
                   options_slim_expire, options_slim_schema)
{
    options_t options = TestType::options();

    // import a simple way...
    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0\n"
                 "n11 v1 dV x10.0 y10.2\n"
                 "n12 v1 dV x10.2 y10.2\n"
                 "n13 v1 dV x10.2 y10.0\n"
                 "w20 v1 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_polygon", options),
                                "osm_id = 20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    // now change the way tags...
    options.append = true;
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v2 dV Thighway=secondary Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_line", options),
                           "osm_id = 20 AND tags->'highway' = 'secondary' AND "
                           "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(0 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));

    // now remove a node from the way...
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v3 dV Thighway=secondary Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_line", options),
                           "osm_id = 20 AND tags->'highway' = 'secondary' AND "
                           "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(0 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));

    // now change the tag back to an area tag (but the way is not closed)...
    REQUIRE_NOTHROW(
        db.run_import(options, "w20 v4 dV Tbuilding=yes Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(0 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));

    // now close the way again
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v5 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_polygon", options),
                                "osm_id = 20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
}

TEMPLATE_TEST_CASE("multipolygons", "", options_slim_default,
                   options_slim_expire, options_slim_schema)
{
    options_t options = TestType::options();

    // import a simple multipolygon relation...
    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0\n"
                 "n11 v1 dV x10.0 y10.2\n"
                 "n12 v1 dV x10.2 y10.2\n"
                 "n13 v1 dV x10.2 y10.0\n"
                 "w20 v1 dV Nn10,n11,n12,n13,n10\n"
                 "r30 v1 dV Ttype=multipolygon,building=yes Mw20@\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_polygon", options),
                                "osm_id = -30 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    // change tags on that relation...
    options.append = true;
    REQUIRE_NOTHROW(db.run_import(
        options,
        "r30 v2 dV Ttype=multipolygon,building=yes,name=Shed Mw20@\n"));

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(1 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));
    REQUIRE(1 == conn.get_count(with_schema("osm2pgsql_test_polygon", options),
                                "osm_id = -30 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    SECTION("remove relation")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r30 v3 dD\n"));
    }

    SECTION("remove multipolygon tag")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "r30 v3 dV Tbuilding=yes,name=Shed Mw20@\n"));
    }

    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_point", options)));
    REQUIRE(0 == conn.get_count(with_schema("osm2pgsql_test_line", options)));
    REQUIRE(0 ==
            conn.get_count(with_schema("osm2pgsql_test_polygon", options)));
}
