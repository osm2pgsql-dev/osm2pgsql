#include "catch.hpp"

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

    REQUIRE_NOTHROW(run_import(options, "n1 Tamenity=restaurant,name=Foobar x12.3 y3"));

    auto conn = db.connect();

}
