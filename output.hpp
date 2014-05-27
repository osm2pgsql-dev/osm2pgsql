/* Common output layer interface */

/* Each output layer must provide methods for 
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc. 
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "middle.hpp"
#include "keyvals.hpp"
#include "reprojection.hpp"
#include "node-ram-cache.hpp"

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

/* Variants for generation of hstore column */
/* No hstore column */
#define HSTORE_NONE 0
/* create a hstore column for all tags which do not have an exclusive column */
#define HSTORE_NORM 1
/* create a hstore column for all tags */
#define HSTORE_ALL 2

/* Scale is chosen such that 40,000 * SCALE < 2^32          */
static const int DEFAULT_SCALE = 100;

struct output_options {
  /* construct with sensible defaults */
  output_options():
	  conninfo(NULL),
	  prefix("planet_osm"),
	  scale(DEFAULT_SCALE),
	  projection(new reprojection(PROJ_SPHERE_MERC)),
	  append(0),
	  slim(0),
	  cache(800),
	  tblsmain_index(NULL),
	  tblsslim_index(NULL),
	  tblsmain_data(NULL),
	  tblsslim_data(NULL),
	  style(OSM2PGSQL_DATADIR "/default.style"),
	  expire_tiles_zoom(-1),
	  expire_tiles_zoom_min(-1),
	  expire_tiles_filename("dirty_tiles"),
	  enable_hstore(HSTORE_NONE),
	  enable_hstore_index(0),
	  enable_multi(0),
	  hstore_columns(NULL),
	  n_hstore_columns(0),
	  keep_coastlines(0),
	  parallel_indexing(1),
#ifdef __amd64__
	  alloc_chunkwise(ALLOC_SPARSE | ALLOC_DENSE),
#else
	  alloc_chunkwise(ALLOC_SPARSE),
#endif
	  num_procs(1),
	  droptemp(0),
	  unlogged(0),
	  hstore_match_only(0),
	  flat_node_cache_enabled(0),
	  excludepoly(0),
	  flat_node_file(NULL),
	  tag_transform_script(NULL)
  {};

  const char *conninfo;  /* Connection info string */
  const char *prefix;    /* prefix for table names */
  int scale;       /* scale for converting coordinates to fixed point */
  boost::shared_ptr<reprojection> projection;  /* SRS of projection */
  int append;      /* Append to existing data */
  int slim;        /* In slim mode */
  int cache;       /* Memory usable for cache in MB */
  const char *tblsmain_index;     /* Pg Tablespace to store indexes on main tables (no default TABLESPACE)*/
  const char *tblsslim_index;     /* Pg Tablespace to store indexes on slim tables (no default TABLESPACE)*/
  const char *tblsmain_data;     /* Pg Tablespace to store main tables (no default TABLESPACE)*/
  const char *tblsslim_data;     /* Pg Tablespace to store slim tables (no default TABLESPACE)*/
  const char *style;     /* style file to use */
  int expire_tiles_zoom;	/* Zoom level for tile expiry list */
  int expire_tiles_zoom_min;	/* Minimum zoom level for tile expiry list */
  const char *expire_tiles_filename;	/* File name to output expired tiles list to */
  int enable_hstore; /* add an additional hstore column with objects key/value pairs */
  int enable_hstore_index; /* add an index on the hstore column */
  int enable_multi; /* Output multi-geometries intead of several simple geometries */
  const char** hstore_columns; /* list of columns that should be written into their own hstore column */
  int n_hstore_columns; /* number of hstore columns */
  int keep_coastlines;
  int parallel_indexing;
  int alloc_chunkwise;
  int num_procs;
  int droptemp; /* drop slim mode temp tables after act */
  int unlogged; /* use unlogged tables where possible */
  int hstore_match_only; /* only copy rows that match an explicitly listed key */
  int flat_node_cache_enabled;
  int excludepoly;
  const char *flat_node_file;
  const char *tag_transform_script;
};

class output_t : public boost::noncopyable {
public:
    output_t(middle_t* mid_, const output_options* options_);
    virtual ~output_t();

    virtual int start() = 0;
    virtual int connect(int startTransaction) = 0;
    virtual middle_t::way_cb_func *way_callback() = 0;
    virtual middle_t::rel_cb_func *relation_callback() = 0;
    virtual void stop() = 0;
    virtual void commit() = 0;
    virtual void cleanup(void) = 0;
    virtual void close(int stopTransaction) = 0;

    virtual int node_add(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_modify(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_delete(osmid_t id) = 0;
    virtual int way_delete(osmid_t id) = 0;
    virtual int relation_delete(osmid_t id) = 0;

    virtual const output_options* get_options()const;

protected:
    output_t();
    middle_t* m_mid;
    const output_options* m_options;
};

unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon);

#endif
