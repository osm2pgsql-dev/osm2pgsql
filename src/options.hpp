#ifndef OSM2PGSQL_OPTIONS_HPP
#define OSM2PGSQL_OPTIONS_HPP

#include "node-ram-cache.hpp"
#include "reprojection.hpp"

#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <vector>

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
class database_options_t
{
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
struct options_t
{
public:
    // fixme: bring back old comment
    options_t();
    /**
     * Parse the options from the command line
     */
    options_t(int argc, char *argv[]);
    virtual ~options_t();

    std::string prefix{"planet_osm"};         ///< prefix for table names
    std::shared_ptr<reprojection> projection; ///< SRS of projection
    bool append = false;                      ///< Append to existing data
    bool slim = false;                        ///< In slim mode
    int cache = 800;                          ///< Memory usable for cache in MB

    /// Pg Tablespace to store indexes on main tables (no default TABLESPACE)
    boost::optional<std::string> tblsmain_index{boost::none};

    /// Pg Tablespace to store indexes on slim tables (no default TABLESPACE)
    boost::optional<std::string> tblsslim_index{boost::none};

    /// Pg Tablespace to store main tables (no default TABLESPACE)
    boost::optional<std::string> tblsmain_data{boost::none};

    /// Pg Tablespace to store slim tables (no default TABLESPACE)
    boost::optional<std::string> tblsslim_data{boost::none};

    std::string style{DEFAULT_STYLE}; ///< style file to use
    uint32_t expire_tiles_zoom = 0;   ///< Zoom level for tile expiry list

    /// Minimum zoom level for tile expiry list
    uint32_t expire_tiles_zoom_min = 0;

    /// Max bbox size in either dimension to expire full bbox for a polygon
    double expire_tiles_max_bbox = 20000.0;

    /// File name to output expired tiles list to
    std::string expire_tiles_filename{"dirty_tiles"};

    /// add an additional hstore column with objects key/value pairs, and what type of hstore column
    int hstore_mode = HSTORE_NONE;

    bool enable_hstore_index = false; ///< add an index on the hstore column

    /// Output multi-geometries intead of several simple geometries
    bool enable_multi = false;

    /// list of columns that should be written into their own hstore column
    std::vector<std::string> hstore_columns;

    bool keep_coastlines = false;
    bool parallel_indexing = true;
    int alloc_chunkwise;
    int num_procs;
    bool droptemp = false; ///< drop slim mode temp tables after act

    /// only copy rows that match an explicitly listed key
    bool hstore_match_only = false;

    bool flat_node_cache_enabled = false;
    bool reproject_area = false;
    boost::optional<std::string> flat_node_file{boost::none};

    /**
     * these options allow you to control the name of the
     * Lua functions which get called in the tag transform
     * script. this is mostly useful in with the "multi"
     * output so that a single script file can be used.
     */
    boost::optional<std::string> tag_transform_script{boost::none};
    boost::optional<std::string> tag_transform_node_func{boost::none};
    boost::optional<std::string> tag_transform_way_func{boost::none};
    boost::optional<std::string> tag_transform_rel_func{boost::none};
    boost::optional<std::string> tag_transform_rel_mem_func{boost::none};

    bool create = false;
    bool long_usage_bool = false;
    bool pass_prompt = false;

    database_options_t database_options;
    std::string output_backend{"pgsql"};
    std::string input_reader{"auto"};
    boost::optional<std::string> bbox{boost::none};
    bool extra_attributes = false;
    bool verbose = false;

    std::vector<std::string> input_files;

private:
    /**
     * Check input options for sanity
     */
    void check_options();
};

#endif // OSM2PGSQL_OPTIONS_HPP
