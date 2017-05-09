#pragma once
#include "tags_storage_t.hpp"

class jsonb_tags_storage_t : public tags_storage_t{

inline const char *decode_upto(const char *src, char *dst) const;

std::string escape_string(std::string const &in, bool escape) const;

public:
    std::string get_column_name() const {return "jsonb";}

void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder) const;

std::string encode_tags(osmium::OSMObject const &obj, bool attrs, bool escape) const;

~jsonb_tags_storage_t(){}
};
