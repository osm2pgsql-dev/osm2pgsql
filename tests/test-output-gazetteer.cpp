#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static void import(char const *opl)
{
    REQUIRE_NOTHROW(db.run_import(testing::opt_t().gazetteer().slim(), opl));
}

static void update(char const *opl)
{
    auto opt = testing::opt_t().gazetteer().slim().append();
    REQUIRE_NOTHROW(db.run_import(opt, opl));
}

static bool has_node(pg::conn_t const &conn, osmid_t id, char const *cls)
{
    auto row_count =
        conn.get_count("place", "osm_type = 'N' "
                                "AND osm_id = {} "
                                "AND class = '{}'"_format(id, cls));

    CHECK(row_count <= 1);

    return row_count == 1;
}

TEST_CASE("Import: Main tags")
{
    import("n1 Tamenity=restaurant,name=Foobar x12.3 y3\n"
           "n2 Thighway=bus_stop,railway=stop,name=X x56.4 y-4\n"
           "n3 Tnatural=no x2 y5\n");

    auto conn = db.connect();

    CHECK(has_node(conn, 1, "amenity"));
    CHECK(has_node(conn, 2, "highway"));
    CHECK(has_node(conn, 2, "railway"));
    CHECK_FALSE(has_node(conn, 3, "natural"));
}

TEST_CASE("Import: Main tags with name")
{
    import("n45 Tlanduse=cemetry x0 y0\n"
           "n54 Tlanduse=cemetry,name=There x3 y5\n"
           "n55 Tname:de=Da,landuse=cemetry x0.0 y6.5\n");

    auto conn = db.connect();

    CHECK_FALSE(has_node(conn, 45, "landuse"));
    CHECK(has_node(conn, 54, "landuse"));
    CHECK(has_node(conn, 55, "landuse"));
}

TEST_CASE("Import: Main tags as fallback")
{
    import("n100 Tjunction=yes,highway=bus_stop x0 y0\n"
           "n101 Tjunction=yes,name=Bar x4 y6\n"
           "n200 Tbuilding=yes,amenity=cafe x3 y7\n"
           "n201 Tbuilding=yes,name=Intersting x4 y5\n"
           "n202 Tbuilding=yes x6 y9\n");

    auto conn = db.connect();

    CHECK_FALSE(has_node(conn, 100, "junction"));
    CHECK(has_node(conn, 101, "junction"));
    CHECK_FALSE(has_node(conn, 200, "building"));
    CHECK(has_node(conn, 201, "building"));
    CHECK_FALSE(has_node(conn, 202, "building"));
}
