#ifndef OPTION_H
#define OPTION_H

#include "node-ram-cache.hpp"
#include "reprojection.hpp"

#include <vector>
#include <string>
#include <memory>
#include <boost/optional.hpp>

/* Variants for generation of hstore column */
/* No hstore column */
#define HSTORE_NONE 0
/* create a hstore column for all tags which do not have an exclusive column */
#define HSTORE_NORM 1
/* create a hstore column for all tags */
#define HSTORE_ALL 2

/* Scale is chosen such that 40,000 * SCALE < 2^32          */
enum { DEFAULT_SCALE = 100 };

struct options_t {
public:
    /* construct with sensible defaults */
    options_t();
    virtual ~options_t();

    static options_t parse(int argc, char *argv[]);

    std::string conninfo; /* Connection info string */
    std::string prefix; /* prefix for table names */
    int scale; /* scale for converting coordinates to fixed point */
    std::shared_ptr<reprojection> projection; /* SRS of projection */
    bool append; /* Append to existing data */
    bool slim; /* In slim mode */
    int cache; /* Memory usable for cache in MB */
    boost::optional<std::string> tblsmain_index; /* Pg Tablespace to store indexes on main tables (no default TABLESPACE)*/
    boost::optional<std::string> tblsslim_index; /* Pg Tablespace to store indexes on slim tables (no default TABLESPACE)*/
    boost::optional<std::string> tblsmain_data; /* Pg Tablespace to store main tables (no default TABLESPACE)*/
    boost::optional<std::string> tblsslim_data; /* Pg Tablespace to store slim tables (no default TABLESPACE)*/
    std::string style; /* style file to use */
    int expire_tiles_zoom; /* Zoom level for tile expiry list */
    int expire_tiles_zoom_min; /* Minimum zoom level for tile expiry list */
    std::string expire_tiles_filename; /* File name to output expired tiles list to */
    int hstore_mode; /* add an additional hstore column with objects key/value pairs, and what type of hstore column */
    bool enable_hstore_index; /* add an index on the hstore column */
    bool enable_multi; /* Output multi-geometries intead of several simple geometries */
    std::vector<std::string> hstore_columns; /* list of columns that should be written into their own hstore column */
    bool keep_coastlines;
    bool parallel_indexing;
    int alloc_chunkwise;
    int num_procs;
    bool droptemp; /* drop slim mode temp tables after act */
    bool unlogged; /* use unlogged tables where possible */
    bool hstore_match_only; /* only copy rows that match an explicitly listed key */
    bool flat_node_cache_enabled;
    bool excludepoly;
    boost::optional<std::string> flat_node_file;
    boost::optional<std::string> tag_transform_script,
        tag_transform_node_func,    // these options allow you to control the name of the
        tag_transform_way_func,     // Lua functions which get called in the tag transform
        tag_transform_rel_func,     // script. this is mostly useful in with the "multi"
        tag_transform_rel_mem_func; // output so that a single script file can be used.

    bool create;
    bool long_usage_bool;
    bool pass_prompt;

    std::string db;
    boost::optional<std::string> username;
    boost::optional<std::string> host;
    boost::optional<std::string> password;
    std::string port;
    std::string output_backend;
    std::string input_reader;
    boost::optional<std::string> bbox;
    bool extra_attributes;
    bool verbose;

    std::vector<std::string> input_files;
private:

};

#endif
