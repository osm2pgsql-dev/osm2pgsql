#ifndef OSM2PGSQL_OUTPUT_HPP
#define OSM2PGSQL_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Common output layer interface.
 */

#include <osmium/index/id_set.hpp>

#include "options.hpp"
#include "osmtypes.hpp"

class db_copy_thread_t;
class thread_pool_t;

struct middle_query_t;

class output_t
{
public:
    /// Factory method for creating instances of classes derived from output_t.
    static std::shared_ptr<output_t>
    create_output(std::shared_ptr<middle_query_t> const &mid,
                  std::shared_ptr<thread_pool_t> thread_pool,
                  options_t const &options);

    output_t(output_t const &) = default;
    output_t &operator=(output_t const &) = default;

    output_t(output_t &&) = default;
    output_t &operator=(output_t &&) = default;

    virtual ~output_t();

    /**
     * This function clones instances of derived classes of output_t, it must
     * be implemented in derived classes.
     */
    virtual std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const = 0;

    /**
     * Remove pointer to middle_query_t from output, so the middle_query_t
     * is properly cleaned up and doesn't hold references to any datastructures
     * any more.
     */
    void free_middle_references();

    virtual void start() = 0;
    virtual void stop() = 0;
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

    virtual void merge_expire_trees(output_t * /*other*/) {}

    output_requirements const &get_requirements() const noexcept
    {
        return m_output_requirements;
    }

protected:
    /**
     * Constructor used for creating a new object using the create_output()
     * function.
     */
    output_t(std::shared_ptr<middle_query_t> mid,
             std::shared_ptr<thread_pool_t> thread_pool,
             options_t const &options);

    /**
     * Constructor used for cloning an existing output using clone(). It gets
     * a new middle query pointer, everything else is copied over.
     */
    output_t(output_t const *other, std::shared_ptr<middle_query_t> mid);

    thread_pool_t &thread_pool() const noexcept
    {
        assert(m_thread_pool);
        return *m_thread_pool;
    }

    middle_query_t const &middle() const noexcept
    {
        assert(m_mid);
        return *m_mid;
    }

    output_requirements &access_requirements() noexcept
    {
        return m_output_requirements;
    }

    options_t const *get_options() const noexcept { return m_options; };

private:
    std::shared_ptr<middle_query_t> m_mid;
    options_t const *m_options;
    std::shared_ptr<thread_pool_t> m_thread_pool;
    output_requirements m_output_requirements{};
};

#endif // OSM2PGSQL_OUTPUT_HPP
