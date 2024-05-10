#ifndef OSM2PGSQL_OSMDATA_HPP
#define OSM2PGSQL_OSMDATA_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * It contains the osmdata_t class.
 */

#include <memory>
#include <string>

#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm/box.hpp>

#include "idlist.hpp"
#include "osmtypes.hpp"
#include "pgsql-params.hpp"

class middle_t;
class output_t;
struct options_t;

/**
 * This class guides the processing of the OSM data through its multiple
 * stages. It calls upon the the middle and the output to do their work.
 *
 * It also does the dependency management, keeping track of the dependencies
 * between OSM objects (nodes in ways and members of relations).
 */
class osmdata_t : public osmium::handler::Handler
{
public:
    osmdata_t(std::shared_ptr<middle_t> mid, std::shared_ptr<output_t> output,
              options_t const &options);

    void node(osmium::Node const &node);
    void way(osmium::Way &way);
    void relation(osmium::Relation const &rel);

    void after_nodes();
    void after_ways();
    void after_relations();

    /**
     * Rest of the processing (stages 1b, 1c, 2, and database postprocessing).
     * This is called once after the input files are processed.
     */
    void stop();

    bool has_pending() const noexcept;

    idlist_t get_pending_way_ids();

    idlist_t get_pending_relation_ids();

private:
    /**
     * Run stage 1b and stage 1c processing: Process dependent objects in
     * append mode.
     */
    void process_dependents();

    /**
     * Run stage 2 processing: Reprocess objects marked in stage 1 (if any).
     */
    void reprocess_marked() const;

    /**
     * Run postprocessing on database: Clustering and index creation.
     */
    void postprocess_database() const;

    /**
     * In append mode all new and changed nodes will be added to this. After
     * all nodes are read this is used to figure out which parent ways and
     * relations reference these nodes. Deleted nodes are not stored in here,
     * because all ways and relations that referenced deleted nodes must be in
     * the change file, too, and so we don't have to find out which ones they
     * are.
     */
    idlist_t m_changed_nodes;

    /**
     * In append mode all new and changed ways will be added to this. After
     * all ways are read this is used to figure out which parent relations
     * reference these ways. Deleted ways are not stored in here, because all
     * relations that referenced deleted ways must be in the change file, too,
     * and so we don't have to find out which ones they are.
     */
    idlist_t m_changed_ways;

    /**
     * In append mode all new and changed relations will be added to this.
     * This is then used to remove already processed relations from the
     * pending list.
     */
    idlist_t m_changed_relations;

    idlist_t m_ways_pending_tracker;
    idlist_t m_rels_pending_tracker;

    std::shared_ptr<middle_t> m_mid;
    std::shared_ptr<output_t> m_output;

    connection_params_t m_connection_params;

    // Bounding box for node import (or invalid Box if everything should be
    // imported).
    osmium::Box m_bbox;

    unsigned int m_num_procs;
    bool m_append;
    bool m_droptemp;
    bool m_with_extra_attrs;
};

#endif // OSM2PGSQL_OSMDATA_HPP
