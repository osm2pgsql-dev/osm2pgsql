#ifndef OSM2PGSQL_OUTPUT_PGSQL_HPP
#define OSM2PGSQL_OUTPUT_PGSQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "output.hpp"
#include "reprojection.hpp"
#include "table.hpp"
#include "tagtransform.hpp"

#include <array>
#include <memory>

class output_pgsql_t : public output_t
{
    output_pgsql_t(output_pgsql_t const *other,
                   std::shared_ptr<middle_query_t> const &mid,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);

public:
    enum table_id
    {
        t_point = 0,
        t_line,
        t_poly,
        t_roads,
        t_MAX
    };

    output_pgsql_t(std::shared_ptr<middle_query_t> const &mid,
                   std::shared_ptr<thread_pool_t> thread_pool,
                   options_t const &options,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);

    ~output_pgsql_t() override;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override;
    void stop() override;
    void sync() override;

    void wait() override;

    void pending_way(osmid_t id) override;
    void pending_relation(osmid_t id) override;

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const &node) override;
    void way_modify(osmium::Way *way) override;
    void relation_modify(osmium::Relation const &rel) override;

    void node_delete(osmid_t id) override;
    void way_delete(osmid_t id) override;
    void relation_delete(osmid_t id) override;

    void merge_expire_trees(output_t *other) override;

protected:
    void pgsql_out_way(osmium::Way const &way, taglist_t *tags, bool polygon,
                       bool roads);
    void pgsql_process_relation(osmium::Relation const &rel);
    void pgsql_delete_way_from_output(osmid_t osm_id);
    void pgsql_delete_relation_from_output(osmid_t osm_id);

    std::unique_ptr<tagtransform_t> m_tagtransform;

    //enable output of a generated way_area tag to either hstore or its own column
    bool m_enable_way_area;

    std::array<std::unique_ptr<table_t>, t_MAX> m_tables;

    std::shared_ptr<reprojection> m_proj;
    expire_tiles m_expire;

    osmium::memory::Buffer m_buffer;
    osmium::memory::Buffer m_rels_buffer;
};

#endif // OSM2PGSQL_OUTPUT_PGSQL_HPP
