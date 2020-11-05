#ifndef OSM2PGSQL_OSMDATA_HPP
#define OSM2PGSQL_OSMDATA_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * It contains the osmdata_t class.
 */

#include <memory>
#include <vector>

#include <osmium/fwd.hpp>
#include <osmium/io/file.hpp>

#include "dependency-manager.hpp"
#include "osmtypes.hpp"
#include "progress-display.hpp"

class options_t;
class output_t;
struct middle_t;
struct slim_middle_t;

/**
 * This class guides the processing of the OSM data through its multiple
 * stages. It calls upon the major compontents of osm2pgsql, the dependency
 * manager, the middle, and the outputs to do their work.
 */
class osmdata_t
{
public:
    osmdata_t(std::unique_ptr<dependency_manager_t> dependency_manager,
              std::shared_ptr<middle_t> mid,
              std::vector<std::shared_ptr<output_t>> outs,
              options_t const &options);

    void start() const;
    void flush() const;

    /**
     * Process the specified OSM file (stage 1a). This is called once for
     * each input file.
     */
    progress_display_t process_file(osmium::io::File const &file,
                                    osmium::Box const &bbox) const;

    /**
     * Rest of the processing (stages 1b, 1c, 2, and database postprocessing).
     * This is called once after process_file() was called for each input file.
     */
    void stop() const;

    void node_add(osmium::Node const &node) const;
    void way_add(osmium::Way *way) const;
    void relation_add(osmium::Relation const &rel) const;

    void node_modify(osmium::Node const &node) const;
    void way_modify(osmium::Way *way) const;
    void relation_modify(osmium::Relation const &rel) const;

    void node_delete(osmid_t id) const;
    void way_delete(osmid_t id) const;
    void relation_delete(osmid_t id) const;

private:

    /**
     * Run stage 1b and stage 1c processing: Process dependent objects in
     * append mode.
     */
    void process_dependents() const;

    /**
     * Run stage 2 processing: Reprocess objects marked in stage 1 (if any).
     */
    void reprocess_marked() const;

    /**
     * Run postprocessing on database: Clustering and index creation.
     */
    void postprocess_database() const;

    slim_middle_t &slim_middle() const noexcept;

    std::unique_ptr<dependency_manager_t> m_dependency_manager;
    std::shared_ptr<middle_t> m_mid;
    std::vector<std::shared_ptr<output_t>> m_outs;

    std::string m_conninfo;
    int m_num_procs;
    bool m_append;
    bool m_droptemp;
    bool m_parallel_indexing;
    bool m_with_extra_attrs;
    bool m_with_forward_dependencies;

};

#endif // OSM2PGSQL_OSMDATA_HPP
