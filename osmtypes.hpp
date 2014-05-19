/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <boost/shared_ptr.hpp>
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
struct output_t;

class osmdata_t {
	public:
		osmdata_t(output_t* out_);
		~osmdata_t();
    
		//TODO: move output to be a private/protected member
		// then steal from it its mid object and its important functions
		// such as add/mod/del. then make output a vector of multiple
		output_t *out;
	private:

};

/* exit_nicely - called to cleanup after fatal error */
void exit_nicely();

#endif
