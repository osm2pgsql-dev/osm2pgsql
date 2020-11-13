#include <catch.hpp>

#include <random>
#include <utility>
#include <vector>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

// Use a random device with a fixed seed. We don't really care about
// the quality of random numbers here, we just need to generate valid
// OSM test data. The fixed seed ensures that the results are reproducible.
static std::mt19937_64 rng{47382}; // NOLINT(cert-msc32-c)

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

    void del(osmid_t id) { fmt::format_to(m_rel_opl, "r{} v2 dD\n", id); }

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

using hstore_item = std::pair<std::string, std::string>;
using hstore_list = std::vector<hstore_item>;

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
        auto const opt = testing::opt_t().gazetteer().slim().append();
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

    void obj_names(pg::conn_t const &conn, osmid_t id, char const *cls,
                   hstore_list const &names)
    {
        hstore_compare(conn, id, cls, "name", names);
    }

    void obj_address(pg::conn_t const &conn, osmid_t id, char const *cls,
                     hstore_list const &names)
    {
        hstore_compare(conn, id, cls, "address", names);
    }

    void obj_extratags(pg::conn_t const &conn, osmid_t id, char const *cls,
                       hstore_list const &names)
    {
        hstore_compare(conn, id, cls, "extratags", names);
    }

    std::string obj_field(pg::conn_t const &conn, osmid_t id, char const *cls,
                          char const *column)
    {
        char const tchar = m_opl_factory.type();
        return conn.require_scalar<std::string>(
            "SELECT {} FROM place WHERE osm_type = '{}' AND osm_id = {}"
            " AND class = '{}'"_format(column, tchar, id, cls));
    }

private:
    void hstore_compare(pg::conn_t const &conn, osmid_t id, char const *cls,
                        char const *column, hstore_list const &names)
    {
        char const tchar = m_opl_factory.type();
        auto const sql =
            "SELECT skeys({}), svals({}) FROM place"
            " WHERE osm_type = '{}' AND osm_id = {}"
            " AND class = '{}'"_format(column, column, tchar, id, cls);
        auto const res = conn.query(PGRES_TUPLES_OK, sql);

        hstore_list actual;
        for (int i = 0; i < res.num_tuples(); ++i) {
            actual.emplace_back(res.get_value(i, 0), res.get_value(i, 1));
        }

        CHECK_THAT(actual, Catch::Matchers::UnorderedEquals(names));
    }

    T m_opl_factory;
};

namespace Catch {
template <>
struct StringMaker<hstore_item>
{
    static std::string convert(hstore_item const &value)
    {
        return "({}, {})"_format(std::get<0>(value), std::get<1>(value));
    }
};
} // namespace Catch

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

        t.add(1, "not_a=restaurant");
        t.add(2, "highway=bus_stop,name=X");
        t.del(3);

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

TEST_CASE("Operator tag")
{
    node_importer_t t;

    t.add(1001, "amenity=restaurant,operator=McDo");
    t.add(1002, "amenity=prison,operator=police");

    t.import();

    auto conn = db.connect();

    t.obj_names(conn, 1001, "amenity", {{"operator", "McDo"}});
    t.obj_names(conn, 1002, "amenity", {});
}

TEST_CASE("Name and reg tags")
{
    node_importer_t t;

    t.add(2001, "highway=road,name=Foo,alt_name:de=Bar,ref=45");
    t.add(2002, "highway=road,name:prefix=Pre,name:suffix=Post,ref:de=55");
    t.add(2003, "highway=yes,name:%20%de=Foo,name=real1");
    t.add(2004, "highway=yes,name:%a%de=Foo,name=real2");
    t.add(2005, "highway=yes,name:%9%de=Foo,name:\\\\=real3");
    t.add(2006, "highway=yes,name:%9%de=Foo,name=rea\\l3");

    t.import();

    auto conn = db.connect();

    t.obj_names(conn, 2001, "highway",
                {{"name", "Foo"}, {"alt_name:de", "Bar"}, {"ref", "45"}});
    t.obj_names(conn, 2002, "highway", {});
    t.obj_extratags(
        conn, 2002, "highway",
        {{"name:prefix", "Pre"}, {"name:suffix", "Post"}, {"ref:de", "55"}});
    t.obj_names(conn, 2003, "highway",
                {{"name: de", "Foo"}, {"name", "real1"}});
    t.obj_names(conn, 2004, "highway",
                {{"name:\nde", "Foo"}, {"name", "real2"}});
    t.obj_names(conn, 2005, "highway",
                {{"name:\tde", "Foo"}, {"name:\\\\", "real3"}});
    t.obj_names(conn, 2006, "highway",
                {{"name:\tde", "Foo"}, {"name", "rea\\l3"}});
}

TEST_CASE("Name when using with_name flag")
{
    node_importer_t t;

    t.add(3001, "bridge=yes,bridge:name=GoldenGate");
    t.add(3002, "bridge=yes,bridge:name:en=Rainbow");

    t.import();

    auto conn = db.connect();

    t.obj_names(conn, 3001, "bridge", {{"name", "GoldenGate"}});
    t.obj_names(conn, 3002, "bridge", {{"name:en", "Rainbow"}});
}

TEST_CASE("Address tags")
{
    node_importer_t t;

    t.add(4001, "addr:housenumber=34,addr:city=Esmarald,addr:county=Land");
    t.add(4002, "addr:streetnumber=10,is_in:city=Rootoo,is_in=Gold");

    t.import();

    auto conn = db.connect();

    t.obj_address(
        conn, 4001, "place",
        {{"housenumber", "34"}, {"city", "Esmarald"}, {"county", "Land"}});
    t.obj_address(
        conn, 4002, "place",
        {{"streetnumber", "10"}, {"city", "Rootoo"}, {"is_in", "Gold"}});
}

TEST_CASE("Country codes")
{
    node_importer_t t;

    t.add(5001, "shop=yes,country_code=DE");
    t.add(5002, "shop=yes,country_code=toolong");
    t.add(5003, "shop=yes,country_code=x");
    t.add(5004, "shop=yes,addr:country=us");
    t.add(5005, "shop=yes,is_in:country=be");

    t.import();

    auto conn = db.connect();

    t.obj_address(conn, 5001, "shop", {{"country", "DE"}});
    t.obj_address(conn, 5002, "shop", {});
    t.obj_address(conn, 5003, "shop", {});
    t.obj_address(conn, 5004, "shop", {{"country", "us"}});
    t.obj_address(conn, 5005, "shop", {});
}

TEST_CASE("Postcodes")
{
    node_importer_t t;

    t.add(6001, "shop=bank,addr:postcode=12345");
    t.add(6002, "shop=bank,tiger:zip_left=34343");
    t.add(6003, "shop=bank,is_in:postcode=9009");

    t.import();

    auto conn = db.connect();

    t.obj_address(conn, 6001, "shop", {{"postcode", "12345"}});
    t.obj_address(conn, 6002, "shop", {{"postcode", "34343"}});
    t.obj_address(conn, 6003, "shop", {});
}

TEST_CASE("Main with extra")
{
    way_importer_t t;

    t.add(7001, "highway=primary,bridge=yes,name=1");

    t.import();

    auto conn = db.connect();

    t.obj_extratags(conn, 7001, "highway", {{"bridge", "yes"}});
}

TEST_CASE("Global fallback and skipping")
{
    node_importer_t t;

    t.add(8001, "shop=shoes,note:de=Nein,xx=yy");
    t.add(8002, "shop=shoes,building=no,ele=234");
    t.add(8003, "shop=shoes,name:source=survey");

    t.import();

    auto conn = db.connect();

    t.obj_extratags(conn, 8001, "shop", {{"xx", "yy"}});
    t.obj_extratags(conn, 8002, "shop", {{"ele", "234"}});
    t.obj_extratags(conn, 8003, "shop", {});
}

TEST_CASE("Admin levels")
{
    node_importer_t t;

    t.add(9001, "place=city");
    t.add(9002, "place=city,admin_level=16");
    t.add(9003, "place=city,admin_level=x");
    t.add(9004, "place=city,admin_level=1");
    t.add(9005, "place=city,admin_level=0");

    t.import();

    auto conn = db.connect();

    CHECK("15" == t.obj_field(conn, 9001, "place", "admin_level"));
    CHECK("15" == t.obj_field(conn, 9002, "place", "admin_level"));
    CHECK("15" == t.obj_field(conn, 9003, "place", "admin_level"));
    CHECK("1" == t.obj_field(conn, 9004, "place", "admin_level"));
    CHECK("15" == t.obj_field(conn, 9005, "place", "admin_level"));
}

TEST_CASE("Administrative boundaries with place tags")
{
    relation_importer_t t;

    t.add(10001, "boundary=administrative,place=city,name=A");
    t.add(10002, "boundary=natural,place=city,name=B");
    t.add(10003, "boundary=administrative,place=island,name=C");

    t.import();

    auto conn = db.connect();

    CHECK(1 == t.obj_count(conn, 10001, "boundary"));
    CHECK(0 == t.obj_count(conn, 10001, "place"));
    t.obj_extratags(conn, 10001, "boundary", {{"place", "city"}});
    CHECK(1 == t.obj_count(conn, 10002, "boundary"));
    CHECK(1 == t.obj_count(conn, 10002, "place"));
    t.obj_extratags(conn, 10002, "boundary", {});
    CHECK(1 == t.obj_count(conn, 10003, "boundary"));
    CHECK(1 == t.obj_count(conn, 10003, "place"));
    t.obj_extratags(conn, 10003, "boundary", {});
}

TEST_CASE("Shorten tiger:county tags")
{
    node_importer_t t;

    t.add(11001, "place=village,tiger:county=Feebourgh%2c%%20%AL");
    t.add(11002,
          "place=village,addr:state=Alabama,tiger:county=Feebourgh%2c%%20%AL");
    t.add(11003, "place=village,tiger:county=Feebourgh");

    t.import();

    auto conn = db.connect();

    t.obj_address(conn, 11001, "place", {{"tiger:county", "Feebourgh county"}});
    t.obj_address(conn, 11002, "place",
                  {{"tiger:county", "Feebourgh county"}, {"state", "Alabama"}});
    t.obj_address(conn, 11003, "place", {{"tiger:county", "Feebourgh county"}});
}

TEST_CASE("Building fallbacks")
{
    node_importer_t t;

    t.add(12001, "tourism=hotel,building=yes");
    t.add(12002, "building=house");
    t.add(12003, "building=shed,addr:housenumber=1");
    t.add(12004, "building=yes,name=Das-Haus");
    t.add(12005, "building=yes,addr:postcode=12345");

    t.import();

    auto conn = db.connect();

    CHECK("hotel" == t.obj_field(conn, 12001, "tourism", "type"));
    CHECK(0 == t.obj_count(conn, 12001, "building"));
    CHECK(0 == t.obj_count(conn, 12002, "building"));
    CHECK("shed" == t.obj_field(conn, 12003, "building", "type"));
    CHECK("yes" == t.obj_field(conn, 12004, "building", "type"));
    CHECK("postcode" == t.obj_field(conn, 12005, "place", "type"));
}

TEST_CASE("Address interpolations")
{
    way_importer_t t;

    t.add(13001, "addr:interpolation=odd");
    t.add(13002, "addr:interpolation=even,place=city");

    t.import();

    auto conn = db.connect();

    CHECK("houses" == t.obj_field(conn, 13001, "place", "type"));
    CHECK("houses" == t.obj_field(conn, 13002, "place", "type"));
    t.obj_extratags(conn, 13002, "place", {{"place", "city"}});
}
