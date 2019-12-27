#include <catch.hpp>

#include <random>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static std::random_device dev;
static std::mt19937 rng(dev());

class node_importer_t
{
public:
    void add(osmid_t id, char const *tags)
    {
        std::uniform_real_distribution<double> dist(-90, 89.99);

        fmt::format_to(m_opl, "n{} T{} x{} y{}\n", id, tags, 2 * dist(rng),
                       dist(rng));
    }

    void del(osmid_t id) { fmt::format_to(m_opl, "n{} v2 dD\n", id); }

    void import()
    {
        REQUIRE_NOTHROW(db.run_import(testing::opt_t().gazetteer().slim(),
                                      fmt::to_string(m_opl).c_str()));
        m_opl.clear();
    }

    void update()
    {
        auto opt = testing::opt_t().gazetteer().slim().append();
        REQUIRE_NOTHROW(db.run_import(opt, fmt::to_string(m_opl).c_str()));
        m_opl.clear();
    }

    unsigned long obj_count(pg::conn_t const &conn, osmid_t id, char const *cls)
    {
        return conn.get_count("place", "osm_type = 'N' "
                                       "AND osm_id = {} "
                                       "AND class = '{}'"_format(id, cls));
    }

private:
    fmt::memory_buffer m_opl;
};

class way_importer_t
{
public:
    void add(osmid_t id, char const *tags)
    {
        osmid_t first_node = m_current_node_id;
        osmid_t last_node = make_nodes();

        fmt::format_to(m_way_opl, "w{} T{} N", id, tags);
        for (osmid_t i = first_node; i <= last_node; ++i) {
            fmt::format_to(m_way_opl, "n{}{}", i, i == last_node ? '\n' : ',');
        }
    }

    void del(osmid_t id) { fmt::format_to(m_way_opl, "w{} v2 dD\n", id); }

    void import() { run_osm2pgsql(false); }
    void update() { run_osm2pgsql(true); }

    unsigned long obj_count(pg::conn_t const &conn, osmid_t id, char const *cls)
    {
        return conn.get_count("place", "osm_type = 'W' "
                                       "AND osm_id = {} "
                                       "AND class = '{}'"_format(id, cls));
    }

private:
    osmid_t make_nodes()
    {
        unsigned num_nodes = std::uniform_int_distribution<unsigned>(2, 8)(rng);

        // compute the start point, all points afterwards are relative
        std::uniform_real_distribution<double> dist(-90, 89.99);
        double x = 2 * dist(rng);
        double y = dist(rng);

        std::uniform_real_distribution<double> diff_dist(-0.01, 0.01);
        for (unsigned i = 0; i < num_nodes; ++i) {
            fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++, x,
                           y);
            double diffx = 0.0, diffy = 0.0;
            do {
                diffx = diff_dist(rng);
                diffy = diff_dist(rng);
            } while (diffx == 0.0 && diffy == 0.0);
            x += diffx;
            y += diffy;
        }

        return m_current_node_id - 1;
    }

    void run_osm2pgsql(bool append)
    {
        auto opt = testing::opt_t().gazetteer().slim();

        if (append)
            opt.append();

        std::string final_opl = fmt::to_string(m_node_opl);
        final_opl += fmt::to_string(m_way_opl);

        REQUIRE_NOTHROW(db.run_import(opt, final_opl.c_str()));

        m_node_opl.clear();
        m_way_opl.clear();
    }

    osmid_t m_current_node_id = 100;
    fmt::memory_buffer m_node_opl;
    fmt::memory_buffer m_way_opl;
};

TEMPLATE_TEST_CASE("Main tags", "", node_importer_t, way_importer_t)
{
    TestType t;

    SECTION("Import main tags as fallback")
    {
        t.add(100, "junction=yes,highway=bus_stop");
        t.add(101, "junction=yes,name=Bar");
        t.add(200, "building=yes,amenity=cafe");
        t.add(201, "building=yes,name=Intersting");
        t.add(202, "building=yes");

        t.import();

        auto conn = db.connect();

        CHECK(0 == t.obj_count(conn, 100, "junction"));
        CHECK(1 == t.obj_count(conn, 101, "junction"));
        CHECK(0 == t.obj_count(conn, 200, "building"));
        CHECK(1 == t.obj_count(conn, 201, "building"));
        CHECK(0 == t.obj_count(conn, 202, "building"));
    }

    SECTION("Main tag deleted")
    {
        t.add(1, "amenity=restaurant");
        t.add(2, "highway=bus_stop,railway=stop,name=X");
        t.add(3, "amenity=prison");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 1, "amenity"));
        CHECK(1 == t.obj_count(conn, 2, "highway"));
        CHECK(1 == t.obj_count(conn, 2, "railway"));
        CHECK(1 == t.obj_count(conn, 3, "amenity"));

        t.del(3);
        t.add(1, "not_a=restaurant");
        t.add(2, "highway=bus_stop,name=X");

        t.update();

        CHECK(0 == t.obj_count(conn, 1, "amenity"));
        CHECK(2 == t.obj_count(conn, 2, "highway"));
        CHECK(0 == t.obj_count(conn, 2, "railway"));
        CHECK(0 == t.obj_count(conn, 3, "amenity"));
    }

    SECTION("Main tag added")
    {
        t.add(1, "atiy=restaurant");
        t.add(2, "highway=bus_stop,name=X");

        t.import();

        auto conn = db.connect();

        CHECK(0 == t.obj_count(conn, 1, "amenity"));
        CHECK(1 == t.obj_count(conn, 2, "highway"));
        CHECK(0 == t.obj_count(conn, 2, "railway"));
        CHECK(0 == t.obj_count(conn, 3, "amenity"));

        t.add(1, "amenity=restaurant");
        t.add(2, "highway=bus_stop,railway=stop,name=X");
        t.add(3, "amenity=prison");

        t.update();

        CHECK(1 == t.obj_count(conn, 1, "amenity"));
        CHECK(2 == t.obj_count(conn, 2, "highway"));
        CHECK(1 == t.obj_count(conn, 2, "railway"));
        CHECK(1 == t.obj_count(conn, 3, "amenity"));
    }

    SECTION("Main tag modified")
    {
        t.add(10, "highway=footway,name=X");
        t.add(11, "amenity=atm");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 10, "highway"));
        CHECK(1 == t.obj_count(conn, 11, "amenity"));

        t.add(10, "highway=path,name=X");
        t.add(11, "highway=primary");

        t.update();

        CHECK(2 == t.obj_count(conn, 10, "highway"));
        CHECK(0 == t.obj_count(conn, 11, "amenity"));
        CHECK(1 == t.obj_count(conn, 11, "highway"));
    }

    SECTION("Main tags with name, name added")
    {
        t.add(45, "landuse=cemetry");
        t.add(46, "building=yes");

        t.import();

        auto conn = db.connect();

        CHECK(0 == t.obj_count(conn, 45, "landuse"));
        CHECK(0 == t.obj_count(conn, 46, "building"));

        t.add(45, "landuse=cemetry,name=TODO");
        t.add(46, "building=yes,addr:housenumber=1");

        t.update();

        CHECK(1 == t.obj_count(conn, 45, "landuse"));
        CHECK(1 == t.obj_count(conn, 46, "building"));
    }

    SECTION("Main tags with name, name removed")
    {
        t.add(45, "landuse=cemetry,name=TODO");
        t.add(46, "building=yes,addr:housenumber=1");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 45, "landuse"));
        CHECK(1 == t.obj_count(conn, 46, "building"));

        t.add(45, "landuse=cemetry");
        t.add(46, "building=yes");

        t.update();

        CHECK(0 == t.obj_count(conn, 45, "landuse"));
        CHECK(0 == t.obj_count(conn, 46, "building"));
    }

    SECTION("Main tags with name, name modified")
    {
        t.add(45, "landuse=cemetry,name=TODO");
        t.add(46, "building=yes,addr:housenumber=1");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 45, "landuse"));
        CHECK(1 == t.obj_count(conn, 46, "building"));

        t.add(45, "landuse=cemetry,name=DONE");
        t.add(46, "building=yes,addr:housenumber=10");

        t.update();

        CHECK(2 == t.obj_count(conn, 45, "landuse"));
        CHECK(2 == t.obj_count(conn, 46, "building"));
    }

    SECTION("Main tag added to address only node")
    {
        t.add(1, "addr:housenumber=345");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 1, "place"));
        CHECK(0 == t.obj_count(conn, 1, "building"));

        t.add(1, "addr:housenumber=345,building=yes");

        t.update();

        CHECK(0 == t.obj_count(conn, 1, "place"));
        CHECK(1 == t.obj_count(conn, 1, "building"));
    }

    SECTION("Main tag removed from address only node")
    {
        t.add(1, "addr:housenumber=345,building=yes");

        t.import();

        auto conn = db.connect();

        CHECK(0 == t.obj_count(conn, 1, "place"));
        CHECK(1 == t.obj_count(conn, 1, "building"));

        t.add(1, "addr:housenumber=345");

        t.update();

        CHECK(1 == t.obj_count(conn, 1, "place"));
        CHECK(0 == t.obj_count(conn, 1, "building"));
    }

    SECTION("Main tags with name key, adding key name")
    {
        t.add(22, "bridge=yes");

        t.import();

        auto conn = db.connect();

        CHECK(0 == t.obj_count(conn, 22, "bridge"));

        t.add(22, "bridge=yes,bridge:name=high");

        t.update();

        CHECK(1 == t.obj_count(conn, 22, "bridge"));
    }

    SECTION("Main tags with name key, deleting key name")
    {
        t.add(22, "bridge=yes,bridge:name=high");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 22, "bridge"));

        t.add(22, "bridge=yes");

        t.update();

        CHECK(0 == t.obj_count(conn, 22, "bridge"));
    }

    SECTION("Main tags with name key, changing key name")
    {
        t.add(22, "bridge=yes,bridge:name=high");

        t.import();

        auto conn = db.connect();

        CHECK(1 == t.obj_count(conn, 22, "bridge"));

        t.add(22, "bridge=yes,bridge:name:en=high");

        t.update();

        CHECK(2 == t.obj_count(conn, 22, "bridge"));
    }
}
