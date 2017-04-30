#include<iostream>

#include "middle/hstore_tags_storage_t.hpp"

void assert_equal(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
    exit(1);
  }
}

void assert_equal(std::string actual, std::string expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
    exit(1);
  }
}

void test_hstore_tags_storage() {
    hstore_tags_storage_t encoder;
    assert_equal(encoder.get_column_name(), "hstore");
}

int main() {
    test_hstore_tags_storage();
    return 0;
}

