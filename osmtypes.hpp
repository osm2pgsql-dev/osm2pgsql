/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <time.h>
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

#include "keyvals.hpp"

struct output_t;

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

typedef enum { FILETYPE_NONE, FILETYPE_OSM, FILETYPE_OSMCHANGE, FILETYPE_PLANETDIFF } filetypes_t;
typedef enum { ACTION_NONE, ACTION_CREATE, ACTION_MODIFY, ACTION_DELETE } actions_t;

struct osmdata_t {
	public:
		osmdata_t();
		~osmdata_t();

		void init(output_t* out_, const int& extra_attributes_, const char* bbox_);
		void realloc_nodes();
		void realloc_members();
		void resetMembers();
		void printStatus();
		int node_wanted(double lat, double lon);


		osmid_t count_node,    max_node;
		osmid_t count_way,     max_way;
		osmid_t count_rel,     max_rel;
		time_t  start_node, start_way, start_rel;

		struct output_t *out;

		/* Since {node,way} elements are not nested we can guarantee the
		values in an end tag must match those of the corresponding
		start tag and can therefore be cached.
		*/
		double node_lon, node_lat;
		struct keyval tags;
		osmid_t *nds;
		int nd_count, nd_max;
		struct member *members;
		int member_count, member_max;
		osmid_t osm_id;
		filetypes_t filetype;
		actions_t action;
		int extra_attributes;
		int parallel_indexing;

	private:
		void parse_bbox(const char* bbox_);
		bool bbox;
		double minlon, minlat, maxlon, maxlat;
};

/* exit_nicely - called to cleanup after fatal error */
void exit_nicely();

#endif
