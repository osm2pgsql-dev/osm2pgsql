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
 * This program is used for accessing generalization functionality. It is
 * experimental and might or might not be integrated into osm2pgsql itself
 * in the future.
 */

#include "canvas.hpp"
#include "command-line-app.hpp"
#include "debug-output.hpp"
#include "expire-output.hpp"
#include "flex-lua-expire-output.hpp"
#include "flex-lua-geom.hpp"
#include "flex-lua-table.hpp"
#include "flex-table.hpp"
#include "format.hpp"
#include "gen-base.hpp"
#include "gen-create.hpp"
#include "logging.hpp"
#include "lua-init.hpp"
#include "lua-setup.hpp"
#include "lua-utils.hpp"
#include "options.hpp"
#include "params.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"
#include "properties.hpp"
#include "tile.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/geom/tile.hpp>
#include <osmium/util/memory.hpp>

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

// Lua can't call functions on C++ objects directly. This macro defines simple
// C "trampoline" functions which are called from Lua which get the current
// context (the genproc_t object) and call the respective function on the
// context object.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define TRAMPOLINE(func_name, lua_name)                                        \
    static int lua_trampoline_##func_name(lua_State *lua_state)                \
    {                                                                          \
        try {                                                                  \
            return static_cast<genproc_t *>(luaX_get_context(lua_state))       \
                ->func_name();                                                 \
        } catch (std::exception const &e) {                                    \
            return luaL_error(lua_state, "Error in '" #lua_name "': %s\n",     \
                              e.what());                                       \
        } catch (...) {                                                        \
            return luaL_error(lua_state,                                       \
                              "Unknown error in '" #lua_name "'.\n");          \
        }                                                                      \
    }

namespace {

constexpr std::size_t const max_force_single_thread = 4;

struct tile_extent
{
    uint32_t xmin = 0;
    uint32_t ymin = 0;
    uint32_t xmax = 0;
    uint32_t ymax = 0;
    bool valid = false;
};

bool table_is_empty(pg_conn_t const &db_connection, std::string const &schema,
                    std::string const &table)
{
    auto const result = db_connection.exec("SELECT 1 FROM {} LIMIT 1",
                                           qualified_name(schema, table));
    return result.num_tuples() == 0;
}

tile_extent get_extent_from_db(pg_conn_t const &db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &column, uint32_t zoom)
{
    if (table_is_empty(db_connection, schema, table)) {
        return {};
    }

    auto const result = db_connection.exec(
        "SELECT ST_XMin(e), ST_YMin(e), ST_XMax(e), ST_YMax(e)"
        " FROM ST_EstimatedExtent('{}', '{}', '{}') AS e",
        schema, table, column);

    if (result.num_tuples() == 0 || result.is_null(0, 0)) {
        return {};
    }

    double const extent_xmin = strtod(result.get_value(0, 0), nullptr);
    double const extent_ymin = strtod(result.get_value(0, 1), nullptr);
    double const extent_xmax = strtod(result.get_value(0, 2), nullptr);
    double const extent_ymax = strtod(result.get_value(0, 3), nullptr);
    log_debug("Extent: ({} {}, {} {})", extent_xmin, extent_ymin, extent_xmax,
              extent_ymax);

    return {osmium::geom::mercx_to_tilex(zoom, extent_xmin),
            osmium::geom::mercy_to_tiley(zoom, extent_ymax),
            osmium::geom::mercx_to_tilex(zoom, extent_xmax),
            osmium::geom::mercy_to_tiley(zoom, extent_ymin), true};
}

tile_extent get_extent_from_db(pg_conn_t const &db_connection,
                               std::string const &default_schema,
                               params_t const &params, uint32_t zoom)
{
    auto const schema = params.get_string("schema", default_schema);
    std::string table;
    if (params.has("src_table")) {
        table = params.get_string("src_table");
    } else if (params.has("src_tables")) {
        table = params.get_string("src_tables");
        auto const n = table.find(',');
        if (n != std::string::npos) {
            table.resize(n);
        }
    } else {
        throw std::runtime_error{"Need 'src_table' or 'src_tables' param."};
    }
    auto const geom_column = params.get_string("geom_column", "geom");
    return get_extent_from_db(db_connection, schema, table, geom_column, zoom);
}

void get_tiles_from_table(pg_conn_t const &connection, std::string const &table,
                          uint32_t zoom,
                          std::vector<std::pair<uint32_t, uint32_t>> *tiles)
{
    auto const result = connection.exec(
        R"(SELECT x, y FROM "{}" WHERE zoom = {})", table, zoom);

    tiles->reserve(result.num_tuples());

    uint32_t const max = 1UL << zoom;
    for (int n = 0; n < result.num_tuples(); ++n) {
        char *end = nullptr;
        auto const x = std::strtoul(result.get_value(n, 0), &end, 10);
        if (*end != '\0' || x >= max) {
            log_error("Ignoring invalid x value in expire table '{}'", table);
            continue;
        }
        auto const y = std::strtoul(result.get_value(n, 1), &end, 10);
        if (*end != '\0' || y >= max) {
            log_error("Ignoring invalid y value in expire table '{}'", table);
            continue;
        }
        tiles->emplace_back(x, y);
    }
}

class tile_processor_t
{
public:
    tile_processor_t(gen_base_t *generalizer, std::size_t num_tiles)
    : m_generalizer(generalizer), m_num_tiles(num_tiles)
    {}

    void operator()(tile_t const &tile)
    {
        log_debug("Processing tile {}/{}/{} ({} of {})...", tile.zoom(),
                  tile.x(), tile.y(), ++m_count, m_num_tiles);
        m_generalizer->process(tile);
    }

private:
    gen_base_t *m_generalizer;
    std::size_t m_count = 0;
    std::size_t m_num_tiles;
};

void run_tile_gen(std::atomic_flag *error_flag,
                  connection_params_t const &connection_params,
                  gen_base_t *master_generalizer, params_t params,
                  uint32_t zoom,
                  std::vector<std::pair<uint32_t, uint32_t>> *queue,
                  std::mutex *mut, unsigned int n)
{
    try {
        logger::init_thread(n);

        log_debug("Started generalizer thread for '{}'.",
                  master_generalizer->strategy());
        pg_conn_t db_connection{connection_params, "gen.tile"};
        std::string const strategy{master_generalizer->strategy()};
        auto generalizer =
            create_generalizer(strategy, &db_connection,
                               master_generalizer->append_mode(), &params);

        while (true) {
            std::pair<uint32_t, uint32_t> p;
            {
                std::lock_guard<std::mutex> const guard{*mut};
                if (queue->empty()) {
                    master_generalizer->merge_timers(*generalizer);
                    break;
                }
                p = queue->back();
                queue->pop_back();
            }

            tile_t const tile{zoom, p.first, p.second};
            log_debug("Processing tile {}/{}/{}...", tile.zoom(), tile.x(),
                      tile.y());
            generalizer->process(tile);
        }
        log_debug("Shutting down generalizer thread.");
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        error_flag->test_and_set();
    } catch (...) {
        log_error("Unknown exception in generalizer thread.");
        error_flag->test_and_set();
    }
}

class genproc_t
{
public:
    genproc_t(std::string const &filename,
              connection_params_t connection_params, std::string dbschema,
              bool append, bool updatable, uint32_t jobs);

    int app_define_table()
    {
        return setup_flex_table(m_lua_state.get(), &m_tables, &m_expire_outputs,
                                m_dbschema, true, m_append);
    }

    int app_define_expire_output()
    {
        return setup_flex_expire_output(m_lua_state.get(), m_dbschema,
                                        &m_expire_outputs);
    }

    int app_run_gen()
    {
        log_debug("Configuring generalizer...");

        if (lua_type(lua_state(), 1) != LUA_TSTRING) {
            throw std::runtime_error{"Argument #1 to 'run_gen' must be a "
                                     "string naming the strategy."};
        }

        std::string const strategy = lua_tostring(lua_state(), 1);
        log_debug("Generalizer strategy '{}'", strategy);

        if (lua_type(lua_state(), 2) != LUA_TTABLE) {
            throw std::runtime_error{"Argument #2 to 'run_gen' must be a "
                                     "table with parameters."};
        }

        auto params = parse_params();

        if (!params.has("schema")) {
            params.set("schema", m_dbschema);
        }

        write_to_debug_log(params, "Params (config):");

        log_debug("Connecting to database...");
        pg_conn_t db_connection{m_connection_params, "gen.proc"};

        log_debug("Creating generalizer...");
        auto generalizer =
            create_generalizer(strategy, &db_connection, m_append, &params);

        log_info("Running generalizer '{}' ({})...", generalizer->name(),
                 generalizer->strategy());

        write_to_debug_log(params, "Params (after initialization):");

        util::timer_t timer_gen;
        if (generalizer->on_tiles()) {
            process_tiles(db_connection, params, generalizer.get());
        } else {
            generalizer->process();
        }

        log_debug("Running generalizer postprocessing...");
        generalizer->post();

        log_debug("Generalizer processing done.");

        log_debug("Timers:");
        for (auto const &timer : generalizer->timers()) {
            log_debug(fmt::format(
                "  {:10} {:>10L}", timer.name() + ":",
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    timer.elapsed())));
        }
        log_info("Finished generalizer '{}' in {}.", generalizer->name(),
                 util::human_readable_duration(timer_gen.stop()));

        return 0;
    }

    int app_run_sql()
    {
        if (lua_type(lua_state(), 1) != LUA_TTABLE) {
            throw std::runtime_error{"Argument #1 to 'run_sql' must be a "
                                     "table with parameters."};
        }

        std::string const description =
            luaX_get_table_string(lua_state(), "description", 1, "Argument #1");

        bool const transaction = luaX_get_table_bool(lua_state(), "transaction",
                                                     1, "Argument #1", false);

        std::string const if_has_rows = luaX_get_table_string(
            lua_state(), "if_has_rows", 1, "Argument #1", "");

        std::vector<std::string> queries;
        if (transaction) {
            queries.emplace_back("BEGIN");
        }

        lua_getfield(lua_state(), 1, "sql");
        int const ltype = lua_type(lua_state(), -1);
        if (ltype == LUA_TSTRING) {
            queries.emplace_back(lua_tostring(lua_state(), -1));
        } else if (ltype == LUA_TTABLE) {
            if (!luaX_is_array(lua_state())) {
                throw std::runtime_error{
                    "Table in 'sql' field must be an array."};
            }
            luaX_for_each(lua_state(), [&]() {
                if (lua_type(lua_state(), -1) != LUA_TSTRING) {
                    throw std::runtime_error{
                        "Table in 'sql' field must only contain strings."};
                }
                queries.emplace_back(lua_tostring(lua_state(), -1));
            });
        } else {
            throw std::runtime_error{
                "Argument #1 must contain a 'sql' string or table field."};
        }

        if (transaction) {
            queries.emplace_back("COMMIT");
        }

        pg_conn_t const db_connection{m_connection_params, "gen.sql"};

        if (m_append && !if_has_rows.empty()) {
            auto const result = db_connection.exec(if_has_rows);
            if (result.num_tuples() == 0) {
                log_info("Not running SQL command: {} (no rows in "
                         "condition result).",
                         description);
                return 0;
            }
        }

        log_info("Running SQL commands: {}.", description);

        util::timer_t timer_sql;
        for (auto const &query : queries) {
            log_debug("Running sql: {}", query);
            db_connection.exec(query);
        }
        log_info("Finished SQL commands in {}.",
                 util::human_readable_duration(timer_sql.stop()));

        return 0;
    }

    void run();

private:
    params_t parse_params()
    {
        params_t params;

        lua_pushnil(lua_state());
        while (lua_next(lua_state(), 2) != 0) {
            if (lua_type(lua_state(), -2) != LUA_TSTRING) {
                throw std::runtime_error{"Argument #2 must have string keys"};
            }
            auto const *key = lua_tostring(lua_state(), -2);

            switch (lua_type(lua_state(), -1)) {
            case LUA_TSTRING:
                params.set(key, lua_tostring(lua_state(), -1));
                break;
            case LUA_TNUMBER:
#if LUA_VERSION_NUM >= 503
                if (lua_isinteger(lua_state(), -1)) {
                    params.set(key, static_cast<int64_t>(
                                        lua_tointeger(lua_state(), -1)));
                } else {
                    params.set(key, static_cast<double>(
                                        lua_tonumber(lua_state(), -1)));
                }
#else
            {
                auto const value =
                    static_cast<double>(lua_tonumber(lua_state(), -1));
                if (std::floor(value) == value) {
                    params.set(key, static_cast<int64_t>(value));
                } else {
                    params.set(key, value);
                }
            }
#endif
                break;
            case LUA_TBOOLEAN:
                params.set(key,
                           static_cast<bool>(lua_toboolean(lua_state(), -1)));
                break;
            case LUA_TNIL:
                break;
            default:
                throw std::runtime_error{"Argument #2 must have string values"};
            }

            lua_pop(lua_state(), 1);
        }
        return params;
    }

    void process_tiles(pg_conn_t const &db_connection, params_t const &params,
                       gen_base_t *generalizer)
    {
        uint32_t const zoom = generalizer->get_zoom();
        std::vector<std::pair<uint32_t, uint32_t>> tile_list;
        if (m_append) {
            auto const table = params.get_string("expire_list");
            log_debug("Running generalizer for expire list from table '{}'...",
                      table);
            get_tiles_from_table(db_connection, table, zoom, &tile_list);
            log_debug("Truncating table '{}'...", table);
            db_connection.exec("TRUNCATE {}", table);
        } else {
            auto const extent =
                get_extent_from_db(db_connection, m_dbschema, params, zoom);

            if (extent.valid) {
                auto const num_tiles = (extent.xmax - extent.xmin + 1) *
                                       (extent.ymax - extent.ymin + 1);
                log_debug("Running generalizer for bounding box x{}-{}, y{}-{}"
                          " on zoom={}...",
                          extent.xmin, extent.xmax, extent.ymin, extent.ymax,
                          zoom);
                tile_list.reserve(num_tiles);
                for (unsigned x = extent.xmin; x <= extent.xmax; ++x) {
                    for (unsigned y = extent.ymin; y <= extent.ymax; ++y) {
                        tile_list.emplace_back(x, y);
                    }
                }
            } else {
                log_debug("Source table empty, nothing to do.");
            }
        }
        log_debug("Need to process {} tiles.", tile_list.size());
        if (m_jobs == 1 || tile_list.size() < max_force_single_thread) {
            log_debug("Running in single-threaded mode.");
            tile_processor_t tp{generalizer, tile_list.size()};
            while (!tile_list.empty()) {
                auto [x, y] = tile_list.back();
                tp({zoom, x, y});
                tile_list.pop_back();
            }
        } else {
            log_debug("Running in multi-threaded mode.");
            std::mutex mut;
            std::vector<std::thread> threads;
            std::atomic_flag error_flag = ATOMIC_FLAG_INIT;
            for (unsigned int n = 1;
                 n <= std::min(m_jobs, static_cast<uint32_t>(tile_list.size()));
                 ++n) {
                threads.emplace_back(run_tile_gen, &error_flag,
                                     m_connection_params, generalizer, params,
                                     zoom, &tile_list, &mut, n);
            }
            for (auto &t : threads) {
                t.join();
            }
            if (error_flag.test_and_set()) {
                throw std::runtime_error{
                    "Error in generalizer thread. Stopping."};
            }
        }
    }

    lua_State *lua_state() const noexcept { return m_lua_state.get(); }

    std::shared_ptr<lua_State> m_lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    std::vector<flex_table_t> m_tables;
    std::vector<expire_output_t> m_expire_outputs;

    connection_params_t m_connection_params;
    std::string m_dbschema;
    uint32_t m_jobs;
    bool m_append;
    bool m_updatable;
}; // class genproc_t

TRAMPOLINE(app_define_table, define_table)
TRAMPOLINE(app_define_expire_output, define_expire_output)
TRAMPOLINE(app_run_gen, run_gen)
TRAMPOLINE(app_run_sql, run_sql)

genproc_t::genproc_t(std::string const &filename,
                     connection_params_t connection_params,
                     std::string dbschema, bool append, bool updatable,
                     uint32_t jobs)
: m_connection_params(std::move(connection_params)),
  m_dbschema(std::move(dbschema)), m_jobs(jobs), m_append(append),
  m_updatable(updatable)
{
    setup_lua_environment(lua_state(), filename, append);

    luaX_add_table_func(lua_state(), "define_table",
                        lua_trampoline_app_define_table);
    luaX_add_table_func(lua_state(), "define_expire_output",
                        lua_trampoline_app_define_expire_output);

    luaX_add_table_func(lua_state(), "run_gen", lua_trampoline_app_run_gen);
    luaX_add_table_func(lua_state(), "run_sql", lua_trampoline_app_run_sql);

    lua_getglobal(lua_state(), "osm2pgsql");
    if (luaL_newmetatable(lua_state(), osm2pgsql_expire_output_name) != 1) {
        throw std::runtime_error{"Internal error: Lua newmetatable failed."};
    }
    lua_pushvalue(lua_state(), -1); // Copy of new metatable

    // Add metatable as osm2pgsql.ExpireOutput so we can access it from Lua
    lua_setfield(lua_state(), -3, "ExpireOutput");

    // Clean up stack
    lua_settop(lua_state(), 0);

    init_geometry_class(lua_state());

    // Load compiled in init.lua
    if (luaL_dostring(lua_state(), lua_init())) {
        throw fmt_error("Internal error in Lua setup: {}.",
                        lua_tostring(lua_state(), -1));
    }

    // Load user config file
    luaX_set_context(lua_state(), this);
    if (luaL_dofile(lua_state(), filename.c_str())) {
        throw fmt_error("Error loading lua config: {}.",
                        lua_tostring(lua_state(), -1));
    }

    write_expire_output_list_to_debug_log(m_expire_outputs);
    write_table_list_to_debug_log(m_tables);
}

void genproc_t::run()
{
    lua_getglobal(lua_state(), "osm2pgsql");
    lua_getfield(lua_state(), -1, "process_gen");

    if (lua_isnil(lua_state(), -1)) {
        log_warn("No function 'osm2pgsql.process_gen()'. Nothing to do.");
        return;
    }

    if (luaX_pcall(lua_state(), 0, 0)) {
        throw fmt_error(
            "Failed to execute Lua function 'osm2pgsql.process_gen': {}.",
            lua_tostring(lua_state(), -1));
    }

    if (!m_append) {
        pg_conn_t const db_connection{m_connection_params, "gen.index"};
        for (auto const &table : m_tables) {
            if (table.id_type() == flex_table_index_type::tile &&
                (table.always_build_id_index() || m_updatable)) {
                log_info("Creating tile (x/y) index on table '{}'...",
                         table.name());
                auto const sql =
                    fmt::format("CREATE INDEX ON {} USING BTREE (x, y) {}",
                                table.full_name(),
                                tablespace_clause(table.index_tablespace()));
                db_connection.exec(sql);
            }
        }
    }
}

} // anonymous namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    try {
        std::string dbschema{"public"};
        std::string middle_dbschema;
        std::string style;
        uint32_t jobs = 1;
        bool append = false;

        command_line_app_t app{
            "osm2pgsql-gen -- Generalize OpenStreetMap data\n"};

        // ------------------------------------------------------------------
        // Main options
        // ------------------------------------------------------------------

        app.add_flag("-a,--append", append)->description("Run in append mode.");

        app.add_option("-S,--style", style)
            ->description("The Lua config/style file (same as for osm2pgsql).")
            ->type_name("FILE");

        app.add_option("-j,--jobs", jobs)
            ->check(CLI::Range(1, 256))
            ->description("Number of parallel jobs (default: 1, max 256).")
            ->type_name("NUM");

        // ------------------------------------------------------------------
        // Database options
        // ------------------------------------------------------------------

        app.add_option("--middle-schema", middle_dbschema)
            ->description("Database schema for middle tables (default: setting "
                          "of --schema).")
            ->type_name("SCHEMA")
            ->group("Database options");

        app.add_option("--schema", dbschema)
            ->description("Database schema (default: 'public').")
            ->type_name("SCHEMA")
            ->group("Database options");

        try {
            app.parse(argc, argv);
        } catch (...) {
            log_info("osm2pgsql-gen version {}", get_osm2pgsql_version());
            throw;
        }

        if (app.want_help()) {
            std::cout << app.help();
            return 0;
        }

        log_info("osm2pgsql-gen version {}", get_osm2pgsql_version());
        log_warn("This is an EXPERIMENTAL extension to osm2pgsql.");

        if (app.want_version()) {
            return 0;
        }

        if (dbschema.empty()) {
            log_error("Schema must not be empty");
            return 2;
        }
        check_identifier(dbschema, "--schema");

        if (middle_dbschema.empty()) {
            middle_dbschema = dbschema;
        } else {
            check_identifier(middle_dbschema, "--middle-schema");
        }

        util::timer_t timer_overall;

        if (append) {
            log_debug("Running in append mode.");
        } else {
            log_debug("Running in create mode.");
        }

        if (jobs == 1) {
            log_debug("Running in single-threaded mode.");
        } else {
            log_debug(
                "Running in multi-threaded mode with a maximum of {} threads.",
                jobs);
        }

        auto const connection_params = app.connection_params();

        log_debug("Checking database capabilities...");
        {
            pg_conn_t const db_connection{connection_params, "gen.check"};
            init_database_capabilities(db_connection);
        }

        properties_t properties{connection_params, middle_dbschema};
        properties.load();

        if (style.empty()) {
            style = properties.get_string("style", "");
            if (style.empty()) {
                log_error("Need --style/-S option");
                return 2;
            }
        }

        if (properties.get_string("output", "flex") != "flex") {
            throw std::runtime_error{
                "osm2pgsql-gen only works with flex output"};
        }

        bool const updatable = properties.get_bool("updatable", false);
        genproc_t gen{style,  connection_params, dbschema,
                      append, updatable,         jobs};
        gen.run();

        osmium::MemoryUsage const mem;
        log_info("Memory: {}MB current, {}MB peak", mem.current(), mem.peak());

        log_info("osm2pgsql-gen took {} overall.",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    } catch (...) {
        log_error("Unknown exception.");
        return 1;
    }

    return 0;
}
