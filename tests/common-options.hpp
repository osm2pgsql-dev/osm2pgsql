#ifndef OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP
#define OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "options.hpp"
#include "reprojection.hpp"

#include "common-pg.hpp"

namespace testing {

class opt_t
{
public:
    opt_t()
    {
        m_opt.output_backend = "pgsql";
        m_opt.prefix = "osm2pgsql_test";
        m_opt.style = OSM2PGSQLDATA_DIR "default.style";
        m_opt.num_procs = 1;
        m_opt.cache = 2;
        m_opt.append = false;
        m_opt.projection = reprojection_t::create_projection(PROJ_SPHERE_MERC);
        m_opt.middle_dbschema = "public";
        m_opt.output_dbschema = "public";
    }

    // Implicit conversion is intended here for ease of use in lots of tests.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    operator options_t() const { return m_opt; }

    opt_t &slim()
    {
        m_opt.slim = true;
        m_opt.middle_database_format = 2;
        return *this;
    }

    opt_t &slim(testing::pg::tempdb_t const &db)
    {
        m_opt.slim = true;
        m_opt.middle_database_format = 2;
        m_opt.connection_params = db.connection_params();
        return *this;
    }

    opt_t &append()
    {
        m_opt.append = true;
        return *this;
    }

    opt_t &flex(char const *style)
    {
        m_opt.output_backend = "flex";
        m_opt.style = TESTDATA_DIR;
        m_opt.style += style;
        return *this;
    }

    opt_t &flatnodes()
    {
        m_opt.flat_node_file = "test_middle_flat.flat.nodes.bin";
        return *this;
    }

    opt_t &style(char const *filename)
    {
        m_opt.style = TESTDATA_DIR;
        m_opt.style += filename;
        return *this;
    }

    opt_t &srs(int srs)
    {
        m_opt.projection = reprojection_t::create_projection(srs);
        return *this;
    }

    opt_t &extra_attributes() noexcept
    {
        m_opt.extra_attributes = true;
        return *this;
    }

    opt_t &schema(char const *schema_name)
    {
        m_opt.dbschema = schema_name;
        m_opt.middle_dbschema = schema_name;
        m_opt.output_dbschema = schema_name;
        return *this;
    }

    opt_t &user(char const *user, char const *password)
    {
        m_opt.connection_params.set("user", user);
        m_opt.connection_params.set("password", password);
        return *this;
    }

private:
    options_t m_opt;
};

} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP
