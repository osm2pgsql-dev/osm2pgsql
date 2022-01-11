#ifndef OSM2PGSQL_OPTIONS_HPP
#define OSM2PGSQL_OPTIONS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <osmium/osm/box.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class reprojection;

/// Variants for generation of hstore column
enum class hstore_column : char
{
    /// no hstore column
    none = 0,
    /// create hstore column for all tags that don't have their own column
    norm = 1,
    /// create hstore column for all tags
    all = 2
};

/**
 * Database options, not specific to a table
 */
struct database_options_t
{
    std::string db;
    std::string username;
    std::string host;
    std::string password;
    std::string port;

    std::string conninfo() const;
};

/**
 * Outputs can signal their requirements to the middle by setting these fields.
 */
struct output_requirements
{
    /**
     * Need full node objects with tags, attributes (only if --extra-attributes
     * is set) and locations. If false, only node locations are needed.
     */
    bool full_nodes = false;

    /**
     * Need full way objects with tags, attributes (only if --extra-attributes
     * is set) and way nodes. If false, only way nodes are needed.
     */
    bool full_ways = false;

    /**
     * Need full relation objects with tags, attributes (only if
     * --extra-attributes is set) and members. If false, no data from relations
     * is needed.
     */
    bool full_relations = false;
};

/**
 * Structure for storing command-line and other options
 */
class options_t
{
public:
    /**
     * Constructor setting default values for all options. Used for testing.
     */
    options_t();

    /**
     * Constructor parsing the options from the command line.
     */
    options_t(int argc, char *argv[]);

    /**
     * Return true if the main program should end directly after the option
     * parsing. This is true when a help text was printed.
     */
    bool early_return() const noexcept {
        return m_print_help;
    }

    std::string prefix{"planet_osm"};         ///< prefix for table names
    std::shared_ptr<reprojection> projection; ///< SRS of projection
    bool append = false;                      ///< Append to existing data
    bool slim = false;                        ///< In slim mode
    int cache = 800;                          ///< Memory usable for cache in MB

    /// Pg Tablespace to store indexes on main tables (no default TABLESPACE)
    std::string tblsmain_index{};

    /// Pg Tablespace to store indexes on slim tables (no default TABLESPACE)
    std::string tblsslim_index{};

    /// Pg Tablespace to store main tables (no default TABLESPACE)
    std::string tblsmain_data{};

    /// Pg Tablespace to store slim tables (no default TABLESPACE)
    std::string tblsslim_data{};

    /// Pg schema to store middle tables in, default none
    std::string middle_dbschema{};

    /// Pg schema to store output tables in, default none
    std::string output_dbschema{};

    std::string style{DEFAULT_STYLE}; ///< style file to use
    uint32_t expire_tiles_zoom = 0;   ///< Zoom level for tile expiry list

    /// Minimum zoom level for tile expiry list
    uint32_t expire_tiles_zoom_min = 0;

    /// Max bbox size in either dimension to expire full bbox for a polygon
    double expire_tiles_max_bbox = 20000.0;

    /// File name to output expired tiles list to
    std::string expire_tiles_filename{"dirty_tiles"};

    /// add an additional hstore column with objects key/value pairs, and what type of hstore column
    hstore_column hstore_mode = hstore_column::none;

    bool enable_hstore_index = false; ///< add an index on the hstore column

    /// Output multi-geometries intead of several simple geometries
    bool enable_multi = false;

    /// list of columns that should be written into their own hstore column
    std::vector<std::string> hstore_columns;

    bool keep_coastlines = false;
    bool parallel_indexing = true;
    unsigned int num_procs;
    bool droptemp = false; ///< drop slim mode temp tables after act

    /**
     * Should changes of objects be propagated forwards (from nodes to ways and
     * from node/way members to parent relations)?
     */
    bool with_forward_dependencies = true;

    /// only copy rows that match an explicitly listed key
    bool hstore_match_only = false;

    bool reproject_area = false;

    /// Name of the flat node file used. Empty if flat node file is not enabled.
    std::string flat_node_file{};

    std::string tag_transform_script;

    bool create = false;
    bool pass_prompt = false;

    database_options_t database_options;
    std::string output_backend{"pgsql"};
    std::string input_format; ///< input file format (default: autodetect)
    osmium::Box bbox;
    bool extra_attributes = false;

    std::vector<std::string> input_files;

    /**
     * How many bits should the node id be shifted for the way node index?
     * Use 0 to disable for backwards compatibility.
     * Currently the default is 0, making osm2pgsql backwards compatible to
     * earlier versions.
     */
    uint8_t way_node_index_id_shift = 0;

private:

    bool m_print_help = false;

    /**
     * Check input options for sanity
     */
    void check_options();
};

#endif // OSM2PGSQL_OPTIONS_HPP
