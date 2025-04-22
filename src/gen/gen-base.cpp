/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-base.hpp"

#include "format.hpp"
#include "params.hpp"
#include "template.hpp"

#include <cassert>

gen_base_t::gen_base_t(pg_conn_t *connection, bool append, params_t *params)
: m_connection(connection), m_params(params), m_append(append)
{
    assert(connection);
    assert(params);

    params->check_identifier_with_default("schema", "");
    auto const schema = params->get_identifier("schema");

    if (params->has("src_table")) {
        auto const src_table = get_params().get_identifier("src_table");
        params->set("src", qualified_name(schema, src_table));
    }

    if (params->has("dest_table")) {
        auto const dest_table = get_params().get_identifier("dest_table");
        params->set("dest", qualified_name(schema, dest_table));
    }

    if (!params->has("geom_column")) {
        params->set("geom_column", "geom");
    }

    m_debug = get_params().get_bool("debug", false);
}

void gen_base_t::check_src_dest_table_params_exist()
{
    if (!m_params->has("src_table")) {
        throw fmt_error("Missing 'src_table' parameter in generalizer{}.",
                        context());
    }

    if (!m_params->has("dest_table")) {
        throw fmt_error("Missing 'dest_table' parameter in generalizer{}.",
                        context());
    }

    if (m_params->get("src_table") == m_params->get("dest_table")) {
        throw fmt_error("The 'src_table' and 'dest_table' parameters "
                        "must be different in generalizer{}.",
                        context());
    }
}

void gen_base_t::check_src_dest_table_params_same()
{
    if (!m_params->has("src_table")) {
        throw fmt_error("Missing 'src_table' parameter in generalizer{}.",
                        context());
    }

    if (m_params->has("dest_table") &&
        m_params->get("dest_table") != m_params->get("src_table")) {
        throw fmt_error("The 'dest_table' parameter must be the same "
                        "as 'src_table' if it exists in generalizer{}.",
                        context());
    }
}

std::string gen_base_t::name() { return get_params().get_string("name", ""); }

std::string gen_base_t::context()
{
    auto const gen_name = name();
    return gen_name.empty() ? "" : fmt::format(" '{}'", gen_name);
}

pg_result_t gen_base_t::dbexec(std::string const &templ)
{
    template_t sql_template{templ};
    sql_template.set_params(get_params());
    return connection().exec(sql_template.render());
}

pg_result_t gen_base_t::dbexec(params_t const &tmp_params,
                               std::string const &templ)
{
    template_t sql_template{templ};
    sql_template.set_params(get_params());
    sql_template.set_params(tmp_params);
    return connection().exec(sql_template.render());
}

void gen_base_t::dbprepare(std::string_view stmt, std::string const &templ)
{
    template_t sql_template{templ};
    sql_template.set_params(get_params());
    connection().prepare(stmt, fmt::runtime(sql_template.render()));
}

void gen_base_t::dbprepare(std::string_view stmt, params_t const &tmp_params,
                           std::string const &templ)
{
    template_t sql_template{templ};
    sql_template.set_params(get_params());
    sql_template.set_params(tmp_params);
    connection().prepare(stmt, fmt::runtime(sql_template.render()));
}

void gen_base_t::raster_table_preprocess(std::string const &table)
{
    params_t tmp_params;
    tmp_params.set("TABLE", table);

    dbexec(tmp_params, "SELECT DropRasterConstraints('{schema}'::name,"
                       " '{TABLE}'::name, 'rast'::name)");
}

void gen_base_t::raster_table_postprocess(std::string const &table)
{
    params_t tmp_params;
    tmp_params.set("TABLE", table);

    dbexec(tmp_params, R"(SELECT AddRasterConstraints('{schema}'::name,)"
                       R"( '{TABLE}'::name, 'rast'::name))");
    dbexec(tmp_params, R"(ALTER TABLE "{schema}"."{TABLE}")"
                       R"( VALIDATE CONSTRAINT enforce_max_extent_rast)");
    dbexec(tmp_params, R"(ANALYZE "{schema}"."{TABLE}")");
}

void gen_base_t::merge_timers(gen_base_t const &other)
{
    for (std::size_t n = 0; n < m_timers.size(); ++n) {
        m_timers[n] += other.m_timers[n];
    }
}
