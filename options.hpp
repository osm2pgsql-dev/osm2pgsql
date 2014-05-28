#ifndef OPTION_H
#define OPTION_H

#include "keyvals.hpp"
#include "node-ram-cache.hpp"
#include "reprojection.hpp"

#include <vector>
#include <string>
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

struct options_t {
public:
    /* construct with sensible defaults */
    options_t();
    virtual ~options_t();

    static options_t parse(int argc, char *argv[]);

    const char *conninfo; /* Connection info string */
    const char *prefix; /* prefix for table names */
    int scale; /* scale for converting coordinates to fixed point */
    boost::shared_ptr<reprojection> projection; /* SRS of projection */
    int append; /* Append to existing data */
    int slim; /* In slim mode */
    int cache; /* Memory usable for cache in MB */
    const char *tblsmain_index; /* Pg Tablespace to store indexes on main tables (no default TABLESPACE)*/
    const char *tblsslim_index; /* Pg Tablespace to store indexes on slim tables (no default TABLESPACE)*/
    const char *tblsmain_data; /* Pg Tablespace to store main tables (no default TABLESPACE)*/
    const char *tblsslim_data; /* Pg Tablespace to store slim tables (no default TABLESPACE)*/
    const char *style; /* style file to use */
    int expire_tiles_zoom; /* Zoom level for tile expiry list */
    int expire_tiles_zoom_min; /* Minimum zoom level for tile expiry list */
    const char *expire_tiles_filename; /* File name to output expired tiles list to */
    int enable_hstore; /* add an additional hstore column with objects key/value pairs */
    int enable_hstore_index; /* add an index on the hstore column */
    int enable_multi; /* Output multi-geometries intead of several simple geometries */
    std::vector<std::string> hstore_columns; /* list of columns that should be written into their own hstore column */
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

    int create;
    int sanitize;
    int long_usage_bool;
    int pass_prompt;
    const char *db;
    const char *username;
    const char *host;
    const char *password;
    const char *port;
    const char *output_backend ;
    const char *input_reader;
    const char *bbox;
    int extra_attributes;
    int verbose;

    std::vector<std::string> input_files;
private:

};

#endif
