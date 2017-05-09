#pragma once
#include "tags_storage_t.hpp"

class hstore_tags_storage_t : public tags_storage_t {

inline const char * decode_upto(const char *src, char *dst);

void escape4hstore(const char *src, std::string& dst, const bool escape);

public:
    std::string get_column_name() {return "hstore";}


void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder);

std::string encode_tags(osmium::OSMObject const &obj, bool attrs, bool escape);

~hstore_tags_storage_t(){}
};

