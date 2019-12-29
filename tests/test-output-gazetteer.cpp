#include <catch.hpp>

#include <random>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

// Use a random device with a fixed seed. We don't really care about
// the quality of random numbers here, we just need to generate valid
// OSM test data. The fixed seed ensures that the results are reproducible.
static std::mt19937_64 rng(47382);

class node_opl_t
{
public:
    void add(osmid_t id, char const *tags)
    {
        std::uniform_real_distribution<double> dist{-90, 89.99};

        fmt::format_to(m_opl, "n{} T{} x{} y{}\n", id, tags, 2 * dist(rng),
                       dist(rng));
    }

    void del(osmid_t id) { fmt::format_to(m_opl, "n{} v2 dD\n", id); }

    std::string get_and_clear_opl()
    {
        std::string ret = fmt::to_string(m_opl);
        m_opl.clear();
        return ret;
    }

    char type() const noexcept { return 'N'; }

private:
    fmt::memory_buffer m_opl;
};

class way_opl_t
{
public:
    void add(osmid_t id, char const *tags)
    {
        osmid_t const first_node = m_current_node_id;
        osmid_t const last_node = make_nodes();

        fmt::format_to(m_way_opl, "w{} T{} N", id, tags);
        for (osmid_t i = first_node; i <= last_node; ++i) {
            fmt::format_to(m_way_opl, "n{}{}", i, i == last_node ? '\n' : ',');
        }
    }

    void del(osmid_t id) { fmt::format_to(m_way_opl, "w{} v2 dD\n", id); }

    std::string get_and_clear_opl()
    {
        std::string final_opl = fmt::to_string(m_node_opl);
        final_opl += fmt::to_string(m_way_opl);

        m_node_opl.clear();
        m_way_opl.clear();

        return final_opl;
    }

    char type() const noexcept { return 'W'; }

private:
    osmid_t make_nodes()
    {
        std::uniform_int_distribution<unsigned> intdist{2, 8};
        unsigned const num_nodes = intdist(rng);

        // compute the start point, all points afterwards are relative
        std::uniform_real_distribution<double> dist{-89.9, 89.9};
        double x = 2 * dist(rng);
        double y = dist(rng);

        std::uniform_real_distribution<double> diff_dist{-0.01, 0.01};
        for (unsigned i = 0; i < num_nodes; ++i) {
            fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++, x,
                           y);
            double diffx = 0.0;
            double diffy = 0.0;
            do {
                diffx = diff_dist(rng);
                diffy = diff_dist(rng);
            } while (diffx == 0.0 && diffy == 0.0);
            x += diffx;
            y += diffy;
        }

        return m_current_node_id - 1;
    }

    osmid_t m_current_node_id = 100;
    fmt::memory_buffer m_node_opl;
    fmt::memory_buffer m_way_opl;
};

class relation_opl_t
{
public:
    void add(osmid_t id, char const *tags)
    {
        osmid_t const first_node = m_current_node_id;
        osmid_t const last_node = make_nodes();

        // create a very simple multipolygon with one closed way
        fmt::format_to(m_way_opl, "w{} N", id, tags);
        for (osmid_t i = first_node; i <= last_node; ++i) {
            fmt::format_to(m_way_opl, "n{},", i);
        }
        fmt::format_to(m_way_opl, "n{}\n", first_node);

        fmt::format_to(m_rel_opl, "r{} Ttype=multipolygon,{} Mw{}@\n", id, tags,
                       id);
    }

    void del(osmid_t id) { fmt::format_to(m_way_opl, "r{} v2 dD\n", id); }

    std::string get_and_clear_opl()
    {
        std::string final_opl = fmt::to_string(m_node_opl);
        final_opl += fmt::to_string(m_way_opl);
        final_opl += fmt::to_string(m_rel_opl);

        m_node_opl.clear();
        m_way_opl.clear();
        m_rel_opl.clear();

        return final_opl;
    }

    char type() const noexcept { return 'R'; }

private:
    osmid_t make_nodes()
    {
        // compute a centre points and compute four corners from this
        std::uniform_real_distribution<double> dist{-89.9, 89.9};
        double const x = 2 * dist(rng);
        double const y = dist(rng);

        std::uniform_real_distribution<double> diff_dist{0.0000001, 0.01};

        fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++,
                       x - diff_dist(rng), y - diff_dist(rng));
        fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++,
                       x - diff_dist(rng), y + diff_dist(rng));
        fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++,
                       x + diff_dist(rng), y + diff_dist(rng));
        fmt::format_to(m_node_opl, "n{} x{} y{}\n", m_current_node_id++,
                       x + diff_dist(rng), y - diff_dist(rng));

        return m_current_node_id - 1;
    }

    osmid_t m_current_node_id = 100;
    fmt::memory_buffer m_node_opl;
    fmt::memory_buffer m_way_opl;
    fmt::memory_buffer m_rel_opl;
};

template <typename T>
class gazetteer_fixture_t
{
public:
    void add(osmid_t id, char const *tags) { m_opl_factory.add(id, tags); }

    void del(osmid_t id) { m_opl_factory.del(id); }

    void import()
    {
        std::string const opl = m_opl_factory.get_and_clear_opl();
        REQUIRE_NOTHROW(
            db.run_import(testing::opt_t().gazetteer().slim(), opl.c_str()));
    }

    void update()
    {
        auto opt = testing::opt_t().gazetteer().slim().append();
        std::string const opl = m_opl_factory.get_and_clear_opl();
        REQUIRE_NOTHROW(db.run_import(opt, opl.c_str()));
    }

    unsigned long obj_count(pg::conn_t const &conn, osmid_t id, char const *cls)
    {
        char const tchar = m_opl_factory.type();
        return conn.get_count("place",
                              "osm_type = '{}' "
                              "AND osm_id = {} "
                              "AND class = '{}'"_format(tchar, id, cls));
    }

private:
    T m_opl_factory;
};

using node_importer_t = gazetteer_fixture_t<node_opl_t>;
using way_importer_t = gazetteer_fixture_t<way_opl_t>;
using relation_importer_t = gazetteer_fixture_t<relation_opl_t>;

TEMPLATE_TEST_CASE("Main tags", "", node_importer_t, way_importer_t,
                   relation_importer_t)
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
