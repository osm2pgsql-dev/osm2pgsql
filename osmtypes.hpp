/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <config.h>

typedef int64_t osmid_t;
#define strtoosmid strtoll
#define PRIdOSMID PRId64
#define POSTGRES_OSMID_TYPE "int8"

enum OsmType { OSMTYPE_WAY, OSMTYPE_NODE, OSMTYPE_RELATION };

struct osmNode {
    double lon;
    double lat;
};

struct member {
    enum OsmType type;
    osmid_t id;
    char *role;
};

#endif
