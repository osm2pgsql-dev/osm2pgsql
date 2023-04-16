/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
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
#include "tile.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/geom/tile.hpp>
#include <osmium/util/memory.hpp>

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

constexpr std::size_t const max_force_single_thread = 4;

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

static void show_help()
{
    fmt::print(R"(osm2pgsql-gen [OPTIONS]
Generalization of OSM data.

This program is EXPERIMENTAL and might change without notice.

Options:
    -h|--help               Print this help text and stop
    -a|--append             Run in append mode
    -c|--create             Run in create mode (default)
    -j|--jobs               Number of parallel jobs (default 1)
    -l|--log-level=LEVEL    Log level (debug, info (default), warn, error)
    --log-sql               Log SQL commands
    --cimg-info             Call info() function of CImg library

Database options:
    -d|--database=DB    The name of the PostgreSQL database to connect to or
                        a PostgreSQL conninfo string.
    -U|--username=NAME  PostgreSQL user name.
    -W|--password       Force password prompt.
    -H|--host=HOST      Database server host name or socket location.
    -P|--port=PORT      Database server port.
)");
}

static char const *const short_options = "acd:hH:j:l:P:S:U:W";

static std::array<option, 20> const long_options = {
    {{"help", no_argument, nullptr, 'h'},
     {"append", no_argument, nullptr, 'a'},
     {"create", no_argument, nullptr, 'c'},
     {"jobs", required_argument, nullptr, 'j'},
     {"database", required_argument, nullptr, 'd'},
     {"user", required_argument, nullptr, 'U'},
     {"host", required_argument, nullptr, 'H'},
     {"port", required_argument, nullptr, 'P'},
     {"password", no_argument, nullptr, 'W'},
     {"log-level", required_argument, nullptr, 'l'},
     {"style", required_argument, nullptr, 'S'},
     {"cimg-info", no_argument, nullptr, 200},
     {"log-sql", no_argument, nullptr, 201},
     {nullptr, 0, nullptr, 0}}};

struct tile_extent
{
    uint32_t xmin = 0;
    uint32_t ymin = 0;
    uint32_t xmax = 0;
    uint32_t ymax = 0;
    bool valid = false;
};

static bool table_is_empty(pg_conn_t const &db_connection,
                           std::string const &schema, std::string const &table)
{
    auto const result = db_connection.exec("SELECT 1 FROM {} LIMIT 1",
                                           qualified_name(schema, table));
    return result.num_tuples() == 0;
}

static tile_extent get_extent_from_db(pg_conn_t const &db_connection,
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

static tile_extent get_extent_from_db(pg_conn_t const &db_connection,
                                      params_t const &params, uint32_t zoom)
{
    auto const schema = params.get_string("schema", "public");
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

static std::vector<std::pair<uint32_t, uint32_t>>
get_tiles_from_table(pg_conn_t const &connection, std::string const &table)
{
    std::vector<std::pair<uint32_t, uint32_t>> tiles;

    auto const result = connection.exec(R"(SELECT x, y FROM "{}")", table);

    for (int n = 0; n < result.num_tuples(); ++n) {
        char *end = nullptr;
        auto const x = std::strtoul(result.get_value(n, 0), &end, 10);
        auto const y = std::strtoul(result.get_value(n, 1), &end, 10);
        tiles.emplace_back(x, y);
    }

    return tiles;
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

void run_tile_gen(std::string const &conninfo, gen_base_t *master_generalizer,
                  params_t params, uint32_t zoom,
                  std::vector<std::pair<uint32_t, uint32_t>> *queue,
                  std::mutex *mut, unsigned int n)
{
    get_logger().init_thread(n);

    log_debug("Started generalizer thread for '{}'.",
              master_generalizer->strategy());
    pg_conn_t db_connection{conninfo};
    std::string const strategy{master_generalizer->strategy()};
    auto generalizer = create_generalizer(strategy, &db_connection, &params);

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

        generalizer->process({zoom, p.first, p.second});
    }
    log_debug("Shutting down generalizer thread.");
}

class genproc_t
{
public:
    genproc_t(std::string const &filename, std::string conninfo, bool append,
              uint32_t jobs);

    int app_define_table()
    {
#if 0
        if (m_calling_context != calling_context::main) {
            throw std::runtime_error{
                "Database tables have to be defined in the"
                " main Lua code, not in any of the callbacks."};
        }
#endif

        return setup_flex_table(m_lua_state.get(), &m_tables, &m_expire_outputs,
                                true, m_append);
    }

    int app_define_expire_output()
    {
        return setup_flex_expire_output(m_lua_state.get(), &m_expire_outputs);
    }

    int app_run_gen()
    {
        log_debug("Running configured generalizer (run {})...", ++m_gen_run);

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

        write_to_debug_log(params, "Params (config):");

        log_debug("Connecting to database...");
        pg_conn_t db_connection{m_conninfo};

        log_debug("Creating generalizer...");
        auto generalizer =
            create_generalizer(strategy, &db_connection, &params);

        log_debug("Generalizer '{}' ({}) initialized.", generalizer->name(),
                  generalizer->strategy());

        if (m_append) {
            params.set("delete_existing", true);
        }

        write_to_debug_log(params, "Params (after initialization):");

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
        log_debug("Finished generalizer '{}' (run {}).", generalizer->name(),
                  m_gen_run);

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
        std::string const sql =
            luaX_get_table_string(lua_state(), "sql", 1, "Argument #1");

        log_debug("Running SQL command: {}.", description);

        util::timer_t timer_sql;
        pg_conn_t const db_connection{m_conninfo};
        db_connection.exec(sql);
        log_info("SQL command took {}.",
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
            tile_list = get_tiles_from_table(db_connection, table);
            log_debug("Truncating table '{}'...", table);
            db_connection.exec("TRUNCATE {}", table);
        } else {
            auto const extent = get_extent_from_db(db_connection, params, zoom);

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
            for (unsigned int n = 1; n <= m_jobs; ++n) {
                threads.emplace_back(run_tile_gen, m_conninfo, generalizer,
                                     params, zoom, &tile_list, &mut, n);
            }
            for (auto &t : threads) {
                t.join();
            }
        }
    }

    lua_State *lua_state() const noexcept { return m_lua_state.get(); }

    std::shared_ptr<lua_State> m_lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    std::vector<flex_table_t> m_tables;
    std::vector<expire_output_t> m_expire_outputs;

    std::string m_conninfo;
    std::size_t m_gen_run = 0;
    uint32_t m_jobs;
    bool m_append;
}; // class genproc_t

TRAMPOLINE(app_define_table, define_table)
TRAMPOLINE(app_define_expire_output, define_expire_output)
TRAMPOLINE(app_run_gen, run_gen)
TRAMPOLINE(app_run_sql, run_sql)

genproc_t::genproc_t(std::string const &filename, std::string conninfo,
                     bool append, uint32_t jobs)
: m_conninfo(std::move(conninfo)), m_jobs(jobs), m_append(append)
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
}

int main(int argc, char *argv[])
{
    try {
        database_options_t database_options;
        std::string log_level;
        std::string style;
        uint32_t jobs = 1;
        bool pass_prompt = false;
        bool append = false;

        int c = 0;
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        while (-1 != (c = getopt_long(argc, argv, short_options,
                                      long_options.data(), nullptr))) {
            switch (c) {
            case 'h':
                show_help();
                return 0;
            case 'a': // --append
                append = true;
                break;
            case 'c': // --create
                append = false;
                break;
            case 'j': // --jons
                jobs =
                    std::clamp(std::strtoul(optarg, nullptr, 10), 1UL, 256UL);
                break;
            case 'd': // --database
                database_options.db = optarg;
                break;
            case 'U': // --username
                database_options.username = optarg;
                break;
            case 'W': // --password
                pass_prompt = true;
                break;
            case 'H': // --host
                database_options.host = optarg;
                break;
            case 'P': // --port
                database_options.port = optarg;
                break;
            case 'l':
                log_level = optarg;
                break;
            case 'S':
                style = optarg;
                break;
            case 200:
                canvas_t::info();
                return 0;
            case 201:
                get_logger().enable_sql();
                break;
            default:
                log_error("Unknown argument");
                return 2;
            }
        }

        if (log_level == "debug") {
            get_logger().set_level(log_level::debug);
        } else if (log_level == "info" || log_level.empty()) {
            get_logger().set_level(log_level::info);
        } else if (log_level == "warn") {
            get_logger().set_level(log_level::warn);
        } else if (log_level == "error") {
            get_logger().set_level(log_level::error);
        } else {
            log_error("Unknown log level: {}. "
                      "Use 'debug', 'info', 'warn', or 'error'.",
                      log_level);
            return 2;
        }

        if (style.empty()) {
            log_error("Need --style/-S option");
            return 2;
        }

        if (jobs < 1 || jobs > 32) {
            log_error("The --jobs/-j parameter must be between 1 and 32.");
            return 2;
        }

        util::timer_t timer_overall;

        log_info("osm2pgsql-gen version {}", get_osm2pgsql_version());
        log_warn("This is an EXPERIMENTAL extension to osm2pgsql.");

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

        if (pass_prompt) {
            database_options.password = util::get_password();
        }
        auto const conninfo = build_conninfo(database_options);

        log_debug("Checking database capabilities...");
        {
            pg_conn_t const db_connection{conninfo};
            init_database_capabilities(db_connection);
        }

        genproc_t gen{style, conninfo, append, jobs};
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
