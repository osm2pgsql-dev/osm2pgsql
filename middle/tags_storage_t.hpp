#pragma once
#include <string>

#include <osmium/builder/osm_object_builder.hpp>

class tags_storage_t {
public:
    virtual std::string get_column_name() const =0;

virtual void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder) const = 0;

virtual std::string encode_tags(osmium::OSMObject const &obj, bool attrs,
                                       bool escape) const = 0;

virtual ~tags_storage_t(){};

};
