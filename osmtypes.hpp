/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <config.h>

/* Use ./configure --enable-64bit-ids to build a version that supports 64bit IDs. */

#ifdef OSMID64
typedef int64_t osmid_t;
#define strtoosmid strtoll
#define PRIdOSMID PRId64
#define POSTGRES_OSMID_TYPE "int8"
#else
typedef int32_t osmid_t;
#define strtoosmid strtol
#define PRIdOSMID PRId32
#define POSTGRES_OSMID_TYPE "int4"
#endif


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

//forward declaration needed here
struct middle_t;
struct output_t;

class osmdata_t {
	public:
		osmdata_t(middle_t* mid_, output_t* out_);
		~osmdata_t();

    void start();
    void stop();
    void cleanup();
    
    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

		//TODO: move output to be a private/protected member
		// then steal from it its mid object and its important functions
		// such as add/mod/del. then make output a vector of multiple
		middle_t* mid;
	private:
		output_t* out;

};

#endif
