#include<iostream>

#include "middle/hstore_tags_storage_t.hpp"

void assert_equal(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << "." << std::endl;
    exit(1);
  }
}

void assert_equal(std::string actual, std::string expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << "." << std::endl;
    exit(1);
  }
}

void assert_true(bool actual) {
  if (!actual) {
    std::cerr << "Assertion must be true!" << std::endl;
    exit(1);
  }
}

void check_hstore_tags_encoding(std::string const & key, std::string const & val, std::string const & result)
{
    hstore_tags_storage_t encoder;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    osmium::builder::WayBuilder builder(buffer);
    osmium::builder::TagListBuilder tl_builder(buffer, &builder);
    tl_builder.add_tag(key, val);
    buffer.commit();
    auto &way = buffer.get<osmium::Way>(0);
    assert_equal(encoder.encode_tags(way, false, true), result);
}

void check_hstore_tags_parsing(std::string const & input, std::string const & key, std::string const & val)
{
    hstore_tags_storage_t encoder;
    osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
    osmium::builder::WayBuilder builder(buffer);
    osmium::builder::TagListBuilder tl_builder(buffer, &builder);
    encoder.pgsql_parse_tags(input.c_str(), tl_builder);
    buffer.commit();
    auto &way = buffer.get<osmium::Way>(0);
    assert_equal(way.tags().size(), 1);
    assert_true(way.tags().has_tag(key.c_str(), val.c_str()));
}

void test_hstore_tags_storage() {
    hstore_tags_storage_t encoder;
    assert_equal(encoder.get_column_name(), "hstore");

    // Check simple case with several tags
    {
        osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
        osmium::builder::WayBuilder builder(buffer);
        osmium::builder::TagListBuilder tl_builder(buffer, &builder);
        tl_builder.add_tag("a", "b");
        tl_builder.add_tag("c", "d");
        buffer.commit();
        auto &way = buffer.get<osmium::Way>(0);
        assert_equal(encoder.encode_tags(way, false, false), "\"a\"=>\"b\",\"c\"=>\"d\" ");
    }

    // Check escaping
    check_hstore_tags_encoding("name with \"", "\"strange\"", "\"name with \\\\\"\"=>\"\\\\\"strange\\\\\"\" ");
    check_hstore_tags_encoding("some\tformatting\nin tag", "true\rway", "\"some\\\\tformatting\\\\nin tag\"=>\"true\\\\rway\" ");

    // Check parsing
    {
        osmium::memory::Buffer buffer(1024, osmium::memory::Buffer::auto_grow::yes);
        osmium::builder::WayBuilder builder(buffer);
        osmium::builder::TagListBuilder tl_builder(buffer, &builder);
        encoder.pgsql_parse_tags("\"a\"=>\"b\", \"c\"=>\"d\"", tl_builder);
        buffer.commit();
        auto &way = buffer.get<osmium::Way>(0);
        assert_equal(way.tags().size(), 2);
        assert_true(way.tags().has_tag("a", "b"));
        assert_true(way.tags().has_tag("c", "d"));
    }
    //Check escaping
    check_hstore_tags_parsing("\"name with \\\"\"=>\"\\\"strange\\\"\"", "name with \"", "\"strange\"");
    check_hstore_tags_parsing("\"some\\\tformatting\\\nin tag\"=>\"true\\\rway\" ", "some\tformatting\nin tag", "true\rway");
    check_hstore_tags_parsing("\"test\"=>\"true\\\\slash\" ", "test", "true\\slash");
}

int main() {
    test_hstore_tags_storage();
    return 0;
}

