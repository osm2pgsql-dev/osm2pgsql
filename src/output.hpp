#ifndef OSM2PGSQL_OUTPUT_HPP
#define OSM2PGSQL_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Common output layer interface */

/* Each output layer must provide methods for
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc.
*/

#include <osmium/index/id_set.hpp>

#include "options.hpp"
#include "osmtypes.hpp"
#include "thread-pool.hpp"

struct middle_query_t;
class db_copy_thread_t;

class output_t
{
public:
    static std::shared_ptr<output_t>
    create_output(std::shared_ptr<middle_query_t> const &mid,
                  options_t const &options);

    output_t(std::shared_ptr<middle_query_t> const &mid,
             options_t const &options);
    virtual ~output_t();

    virtual std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const = 0;

    virtual void start() = 0;
    virtual void stop(thread_pool_t *pool) = 0;
    virtual void sync() = 0;

    virtual void wait() {}

    virtual osmium::index::IdSetSmall<osmid_t> const &get_marked_way_ids()
    {
        static osmium::index::IdSetSmall<osmid_t> const ids{};
        return ids;
    }

    virtual void reprocess_marked() {}

    virtual void pending_way(osmid_t id) = 0;
    virtual void pending_relation(osmid_t id) = 0;
    virtual void pending_relation_stage1c(osmid_t) {}

    virtual void select_relation_members(osmid_t) {}

    virtual void node_add(osmium::Node const &node) = 0;
    virtual void way_add(osmium::Way *way) = 0;
    virtual void relation_add(osmium::Relation const &rel) = 0;

    virtual void node_modify(osmium::Node const &node) = 0;
    virtual void way_modify(osmium::Way *way) = 0;
    virtual void relation_modify(osmium::Relation const &rel) = 0;

    virtual void node_delete(osmid_t id) = 0;
    virtual void way_delete(osmid_t id) = 0;
    virtual void relation_delete(osmid_t id) = 0;

    const options_t *get_options() const;

    virtual void merge_expire_trees(output_t *other);

    struct output_requirements const &get_requirements() const noexcept
    {
        return m_output_requirements;
    }

protected:
    std::shared_ptr<middle_query_t> m_mid;
    const options_t m_options;
    output_requirements m_output_requirements{};
};

#endif // OSM2PGSQL_OUTPUT_HPP
