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

/**
 * Database options, not specific to a table
 */
class database_options_t {
public:
    database_options_t();
    boost::optional<std::string> db;
    boost::optional<std::string> username;
    boost::optional<std::string> host;
    boost::optional<std::string> password;
    boost::optional<std::string> port;

    std::string conninfo() const;
};

/**
 * Structure for storing command-line and other options
 */
struct options_t {
public:
  // fixme: bring back old comment
    options_t();
    /**
     * Parse the options from the command line
     */
    options_t(int argc, char *argv[]);
    virtual ~options_t();

    std::string prefix; ///< prefix for table names
    std::shared_ptr<reprojection> projection; ///< SRS of projection
    bool append; ///< Append to existing data
    bool slim; ///< In slim mode
    int cache; ///< Memory usable for cache in MB
    boost::optional<std::string> tblsmain_index; ///< Pg Tablespace to store indexes on main tables (no default TABLESPACE)
    boost::optional<std::string> tblsslim_index; ///< Pg Tablespace to store indexes on slim tables (no default TABLESPACE)
    boost::optional<std::string> tblsmain_data; ///< Pg Tablespace to store main tables (no default TABLESPACE)
    boost::optional<std::string> tblsslim_data; ///< Pg Tablespace to store slim tables (no default TABLESPACE)
    std::string style; ///< style file to use
    uint32_t expire_tiles_zoom = 0; ///< Zoom level for tile expiry list
    uint32_t expire_tiles_zoom_min =
        0;                        ///< Minimum zoom level for tile expiry list
    double expire_tiles_max_bbox; ///< Max bbox size in either dimension to expire full bbox for a polygon
    std::string expire_tiles_filename; ///< File name to output expired tiles list to
    int hstore_mode; ///< add an additional hstore column with objects key/value pairs, and what type of hstore column
    bool enable_hstore_index; ///< add an index on the hstore column
    bool enable_multi; ///< Output multi-geometries intead of several simple geometries
    std::vector<std::string> hstore_columns; ///< list of columns that should be written into their own hstore column
    bool keep_coastlines;
    bool parallel_indexing;
    int alloc_chunkwise;
    int num_procs;
    bool droptemp; ///< drop slim mode temp tables after act
    bool hstore_match_only; ///< only copy rows that match an explicitly listed key
    bool flat_node_cache_enabled;
    bool reproject_area;
    boost::optional<std::string> flat_node_file;
    /**
     * these options allow you to control the name of the
     * Lua functions which get called in the tag transform
     * script. this is mostly useful in with the "multi"
     * output so that a single script file can be used.
     */
    boost::optional<std::string> tag_transform_script,
        tag_transform_node_func,
        tag_transform_way_func,
        tag_transform_rel_func,
        tag_transform_rel_mem_func;

    bool create;
    bool long_usage_bool;
    bool pass_prompt;

    database_options_t database_options;
    std::string output_backend;
    std::string input_reader;
    boost::optional<std::string> bbox;
    bool extra_attributes;
    bool verbose;

    std::vector<std::string> input_files;
private:
    /**
     * Check input options for sanity
     */
    void check_options();
};

#endif
