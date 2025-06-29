#ifndef OSM2PGSQL_GEN_BASE_HPP
#define OSM2PGSQL_GEN_BASE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

class params_t;
class tile_t;

/**
 * Base class for generalization strategies.
 */
class gen_base_t
{
public:
    virtual ~gen_base_t() = default;

    /// Process data. Used for non-tile-based generalizers.
    virtual void process() {}

    /// Process one tile. Used for tile-based generalizers.
    virtual void process(tile_t const & /*tile*/) {}

    /// Optional postprocessing after all tiles.
    virtual void post() {}

    /// Get the name of the generalization strategy.
    virtual std::string_view strategy() const noexcept = 0;

    virtual bool on_tiles() const noexcept { return false; }

    virtual uint32_t get_zoom() const noexcept { return 0; }

    void merge_timers(gen_base_t const &other);

    std::vector<util::timer_t> const &timers() const noexcept
    {
        return m_timers;
    }

    bool debug() const noexcept { return m_debug; }

    std::string name();

    std::string context();

    template <typename... TArgs>
    void log_gen(fmt::format_string<TArgs...> format_str, TArgs &&...args)
    {
        if (m_debug) {
            log_debug(format_str, std::forward<TArgs>(args)...);
        }
    }

    bool append_mode() const noexcept { return m_append; }

protected:
    gen_base_t(pg_conn_t *connection, bool append, params_t *params);

    /**
     * Check that the 'src_table' and 'dest_table' parameters exist and that
     * they are different.
     */
    void check_src_dest_table_params_exist();

    /**
     * Check that the 'src_table' parameter exists. If the 'dest_table'
     * parameter exists it must be the same as 'src_table'.
     */
    void check_src_dest_table_params_same();

    pg_conn_t &connection() noexcept { return *m_connection; }

    std::size_t add_timer(char const *name)
    {
        m_timers.emplace_back(name);
        return m_timers.size() - 1;
    }

    util::timer_t &timer(std::size_t n) noexcept { return m_timers[n]; }

    params_t const &get_params() const noexcept { return *m_params; }

    pg_result_t dbexec(std::string const &templ);

    pg_result_t dbexec(params_t const &tmp_params, std::string const &templ);

    void dbprepare(std::string const &stmt, std::string const &templ);

    void dbprepare(std::string const &stmt, params_t const &tmp_params,
                   std::string const &templ);

    void raster_table_preprocess(std::string const &table);

    void raster_table_postprocess(std::string const &table);

private:
    std::vector<util::timer_t> m_timers;
    pg_conn_t *m_connection;
    params_t *m_params;
    bool m_append;
    bool m_debug = false;
}; // class gen_base_t

#endif // OSM2PGSQL_GEN_BASE_HPP
