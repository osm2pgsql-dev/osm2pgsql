#include "catch.hpp"

#include <boost/format.hpp>

#include <osmium/io/file.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

#include "middle-ram.hpp"
#include "output-gazetteer.hpp"
#include "osmdata.hpp"
#include "parse-osmium.hpp"

#include "configs.hpp"
#include "output-feed.hpp"

static pg::tempdb_t db;

using fmt = boost::format;

static pg::result_t require_place(pg::conn_t const &conn, char type, osmid_t id,
                          char const *cls, char const *typ)
{
    return conn.require_row((fmt("SELECT * FROM place WHERE osm_type = '%1%' AND osm_id = %2% AND class = '%3%' AND type = '%4%'")
                             % type % id % cls % typ).str());
}

static void require_place_not(pg::conn_t const &conn, char type,
                              osmid_t id, char const *cls)
{
    REQUIRE(conn.require_scalar<int>((fmt("SELECT count(*) FROM place WHERE osm_type = '%1%' AND osm_id = %2% AND class = '%3%'")
                             % type % id % cls).str()) == 0);
}


class test_parse_t : public parse_osmium_t
{
public:
    using parse_osmium_t::parse_osmium_t;

    void stream_buffer(char const *buf, std::string const &fmt)
    {
        osmium::io::File infile(buf, strlen(buf), fmt);

        osmium::io::Reader reader(infile);
        osmium::apply(reader, *this);
        reader.close();
    }

};

static void run_import(options_t const &options, char const *data, std::string const &fmt = "opl")
{
    // setup the middle
    std::shared_ptr<middle_t> middle(new middle_ram_t(&options));
    middle->start();

    // setup the output
    auto outputs = output_t::create_outputs(
            middle->get_query_instance(middle), options);

    //let osmdata orchestrate between the middle and the outs
    osmdata_t osmdata(middle, outputs);

    osmdata.start();

    test_parse_t parser(options.bbox, options.append, &osmdata);

    parser.stream_buffer(data, fmt);

    osmdata.stop();

}

TEST_CASE("output_gazetteer_t import")
{
    options_t options = testing::options::gazetteer_default(db);

    SECTION("Main tags")
    {
        REQUIRE_NOTHROW(run_import(options,
                    "n1 Tamenity=restaurant,name=Foobar x12.3 y3\n"
                    "n2 Thighway=bus_stop,railway=stop,name=X x56.4 y-4\n"
                    "n3 Tnatural=no x2 y5\n"));

        auto conn = db.connect();

        require_place(conn, 'N', 1, "amenity", "restaurant");
        require_place(conn, 'N', 2, "highway", "bus_stop");
        require_place(conn, 'N', 2, "railway", "stop");
        require_place_not(conn, 'N', 3, "natural");
    }

    SECTION("Main tags with name")
    {
        REQUIRE_NOTHROW(run_import(options,
                    "n45 Tlanduse=cemetry x0 y0\n"
                    "n54 Tlanduse=cemetry,name=There x3 y5\n"
                    "n55 Tname:de=Da,landuse=cemetry x0.0 y6.5\n"
                    ));

        auto conn = db.connect();

        require_place_not(conn, 'N', 45, "landuse");
        require_place(conn, 'N', 54, "landuse", "cemetry");
        require_place(conn, 'N', 55, "landuse", "cemetry");
    }

    SECTION("Main tags as fallback")
    {
        REQUIRE_NOTHROW(run_import(options,
                    "n100 Tjunction=yes,highway=bus_stop x0 y0\n"
                    "n101 Tjunction=yes x4 y6\n"
                    "n200 Tbuilding=yes,amenity=cafe x3 y7\n"
                    "n201 Tbuilding=yes,name=Intersting x4 y5\n"
                    "n202 Tbuilding=yes x6 y9\n"
                    ));

        auto conn = db.connect();

        require_place_not(conn, 'N', 100, "junction");
        require_place(conn, 'N', 101, "junction", "yes");
        require_place_not(conn, 'N', 200, "building");
        require_place(conn, 'N', 201, "building", "yes");
        require_place_not(conn, 'N', 202, "building");

    }
}
