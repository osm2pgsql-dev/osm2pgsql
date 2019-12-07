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

static unsigned long node_count(pg::conn_t const &conn, osmid_t id,
                                char const *cls)
{
    return conn.get_count("place", "osm_type = 'N' "
                                   "AND osm_id = {} "
                                   "AND class = '{}'"_format(id, cls));
}

TEST_CASE("Import main tags as fallback")
{
    import("n100 Tjunction=yes,highway=bus_stop x0 y0\n"
           "n101 Tjunction=yes,name=Bar x4 y6\n"
           "n200 Tbuilding=yes,amenity=cafe x3 y7\n"
           "n201 Tbuilding=yes,name=Intersting x4 y5\n"
           "n202 Tbuilding=yes x6 y9\n");

    auto conn = db.connect();

    CHECK(0 == node_count(conn, 100, "junction"));
    CHECK(1 == node_count(conn, 101, "junction"));
    CHECK(0 == node_count(conn, 200, "building"));
    CHECK(1 == node_count(conn, 201, "building"));
    CHECK(0 == node_count(conn, 202, "building"));
}

TEST_CASE("Main tag deleted")
{
    import("n1 Tamenity=restaurant x12.3 y3\n"
           "n2 Thighway=bus_stop,railway=stop,name=X x56.4 y-4\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 1, "amenity"));
    CHECK(1 == node_count(conn, 2, "highway"));
    CHECK(1 == node_count(conn, 2, "railway"));

    update("n1 Tnot_a=restaurant x12.3 y3\n"
           "n2 Thighway=bus_stop,name=X x56.4 y-4\n");

    CHECK(0 == node_count(conn, 1, "amenity"));
    CHECK(2 == node_count(conn, 2, "highway"));
    CHECK(0 == node_count(conn, 2, "railway"));
}

TEST_CASE("Main tag added")
{
    import("n1 Tatiy=restaurant x12.3 y3\n"
           "n2 Thighway=bus_stop,name=X x56.4 y-4\n");

    auto conn = db.connect();

    CHECK(0 == node_count(conn, 1, "amenity"));
    CHECK(1 == node_count(conn, 2, "highway"));
    CHECK(0 == node_count(conn, 2, "railway"));

    update("n1 Tamenity=restaurant x12.3 y3\n"
           "n2 Thighway=bus_stop,railway=stop,name=X x56.4 y-4\n");

    CHECK(1 == node_count(conn, 1, "amenity"));
    CHECK(2 == node_count(conn, 2, "highway"));
    CHECK(1 == node_count(conn, 2, "railway"));
}

TEST_CASE("Main tag modified")
{
    import("n10 Thighway=footway,name=X x10 y11\n"
           "n11 Tamenity=atm x-10 y-11\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 10, "highway"));
    CHECK(1 == node_count(conn, 11, "amenity"));

    update("n10 Thighway=path,name=X x10 y11\n"
           "n11 Thighway=primary x-10 y-11\n");

    CHECK(2 == node_count(conn, 10, "highway"));
    CHECK(0 == node_count(conn, 11, "amenity"));
    CHECK(1 == node_count(conn, 11, "highway"));
}

TEST_CASE("Main tags with name, name added")
{
    import("n45 Tlanduse=cemetry x0 y0\n"
           "n46 Tbuilding=yes x1 y1\n");

    auto conn = db.connect();

    CHECK(0 == node_count(conn, 45, "landuse"));
    CHECK(0 == node_count(conn, 46, "building"));

    update("n45 Tlanduse=cemetry,name=TODO x0 y0\n"
           "n46 Tbuilding=yes,addr:housenumber=1 x1 y1\n");

    CHECK(1 == node_count(conn, 45, "landuse"));
    CHECK(1 == node_count(conn, 46, "building"));
}

TEST_CASE("Main tags with name, name removed")
{
    import("n45 Tlanduse=cemetry,name=TODO x0 y0\n"
           "n46 Tbuilding=yes,addr:housenumber=1 x1 y1\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 45, "landuse"));
    CHECK(1 == node_count(conn, 46, "building"));

    update("n45 Tlanduse=cemetry x0 y0\n"
           "n46 Tbuilding=yes x1 y1\n");

    CHECK(0 == node_count(conn, 45, "landuse"));
    CHECK(0 == node_count(conn, 46, "building"));
}

TEST_CASE("Main tags with name, name modified")
{
    import("n45 Tlanduse=cemetry,name=TODO x0 y0\n"
           "n46 Tbuilding=yes,addr:housenumber=1 x1 y1\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 45, "landuse"));
    CHECK(1 == node_count(conn, 46, "building"));

    update("n45 Tlanduse=cemetry,name=DONE x0 y0\n"
           "n46 Tbuilding=yes,addr:housenumber=10 x1 y1\n");

    CHECK(2 == node_count(conn, 45, "landuse"));
    CHECK(2 == node_count(conn, 46, "building"));
}

TEST_CASE("Main tag added to address only node")
{
    import("n1 Taddr:housenumber=345 x0 y0\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 1, "place"));
    CHECK(0 == node_count(conn, 1, "building"));

    update("n1 Taddr:housenumber=345,building=yes x0 y0\n");

    CHECK(0 == node_count(conn, 1, "place"));
    CHECK(1 == node_count(conn, 1, "building"));
}

TEST_CASE("Main tag removed from address only node")
{
    import("n1 Taddr:housenumber=345,building=yes x0 y0\n");

    auto conn = db.connect();

    CHECK(0 == node_count(conn, 1, "place"));
    CHECK(1 == node_count(conn, 1, "building"));

    update("n1 Taddr:housenumber=345 x0 y0\n");

    CHECK(1 == node_count(conn, 1, "place"));
    CHECK(0 == node_count(conn, 1, "building"));
}

TEST_CASE("Main tags with name key, adding key name")
{
    import("n22 Tbridge=yes x0 y0\n");

    auto conn = db.connect();

    CHECK(0 == node_count(conn, 22, "bridge"));

    update("n22 Tbridge=yes,bridge:name=high x0 y0\n");

    CHECK(1 == node_count(conn, 22, "bridge"));
}

TEST_CASE("Main tags with name key, deleting key name")
{
    import("n22 Tbridge=yes,bridge:name=high x0 y0\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 22, "bridge"));

    update("n22 Tbridge=yes x0 y0\n");

    CHECK(0 == node_count(conn, 22, "bridge"));
}

TEST_CASE("Main tags with name key, changing key name")
{
    import("n22 Tbridge=yes,bridge:name=high x0 y0\n");

    auto conn = db.connect();

    CHECK(1 == node_count(conn, 22, "bridge"));

    update("n22 Tbridge=yes,bridge:name:en=high x0 y0\n");

    CHECK(2 == node_count(conn, 22, "bridge"));
}
