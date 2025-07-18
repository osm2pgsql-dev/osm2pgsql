#ifndef OSM2PGSQL_OUTPUT_NULL_HPP
#define OSM2PGSQL_OUTPUT_NULL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements dummy output-layer processing for testing.
*/

#include "output.hpp"

class output_null_t : public output_t
{
public:
    output_null_t(std::shared_ptr<middle_query_t> const &mid,
                  std::shared_ptr<thread_pool_t> thread_pool,
                  options_t const &options);

    output_null_t(output_null_t const &) = default;
    output_null_t &operator=(output_null_t const &) = default;

    output_null_t(output_null_t &&) = default;
    output_null_t &operator=(output_null_t &&) = default;

    ~output_null_t() override;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override {}
    void stop() override {}
    void sync() override {}
    void cleanup() {}

    void pending_way(osmid_t) override {}
    void pending_relation(osmid_t) override {}

    void node_add(osmium::Node const & /*node*/) override {}
    void way_add(osmium::Way * /*way*/) override {}
    void relation_add(osmium::Relation const & /*rel*/) override {}

    void node_modify(osmium::Node const & /*node*/) override {}
    void way_modify(osmium::Way * /*way*/) override {}
    void relation_modify(osmium::Relation const & /*rel*/) override {}

    void node_delete(osmium::Node const & /*node*/) override {}
    void way_delete(osmium::Way * /*way*/) override {}
    void relation_delete(osmium::Relation const & /*rel*/) override {}
};

#endif // OSM2PGSQL_OUTPUT_NULL_HPP
