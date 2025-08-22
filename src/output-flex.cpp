/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "output-flex.hpp"

#include "db-copy.hpp"
#include "debug-output.hpp"
#include "expire-output.hpp"
#include "expire-tiles.hpp"
#include "flex-index.hpp"
#include "flex-lua-expire-output.hpp"
#include "flex-lua-geom.hpp"
#include "flex-lua-index.hpp"
#include "flex-lua-locator.hpp"
#include "flex-lua-table.hpp"
#include "flex-lua-wrapper.hpp"
#include "flex-write.hpp"
#include "format.hpp"
#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "logging.hpp"
#include "lua-init.hpp"
#include "lua-setup.hpp"
#include "lua-utils.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"
#include "projection.hpp"
#include "properties.hpp"
#include "reprojection.hpp"
#include "thread-pool.hpp"
#include "util.hpp"
#include "version.hpp"
#include "wkb.hpp"

#include <osmium/osm/types_from_string.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// Mutex used to coordinate access to Lua code
std::mutex lua_mutex;

// Lua can't call functions on C++ objects directly. This macro defines simple
// C "trampoline" functions which are called from Lua which get the current
// context (the output_flex_t object) and call the respective function on the
// context object.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define TRAMPOLINE(func_name, lua_name)                                        \
    int lua_trampoline_##func_name(lua_State *lua_state)                       \
    {                                                                          \
        try {                                                                  \
            return static_cast<output_flex_t *>(luaX_get_context(lua_state))   \
                ->func_name();                                                 \
        } catch (std::exception const &e) {                                    \
            return luaL_error(lua_state, "Error in '" #lua_name "': %s\n",     \
                              e.what());                                       \
        } catch (...) {                                                        \
            return luaL_error(lua_state,                                       \
                              "Unknown error in '" #lua_name "'.\n");          \
        }                                                                      \
    }

TRAMPOLINE(app_define_locator, define_locator)
TRAMPOLINE(app_define_table, define_table)
TRAMPOLINE(app_define_expire_output, define_expire_output)
TRAMPOLINE(app_get_bbox, get_bbox)

TRAMPOLINE(app_as_point, as_point)
TRAMPOLINE(app_as_linestring, as_linestring)
TRAMPOLINE(app_as_polygon, as_polygon)
TRAMPOLINE(app_as_multipoint, as_multipoint)
TRAMPOLINE(app_as_multilinestring, as_multilinestring)
TRAMPOLINE(app_as_multipolygon, as_multipolygon)
TRAMPOLINE(app_as_geometrycollection, as_geometrycollection)

} // anonymous namespace

TRAMPOLINE(table_insert, insert)

prepared_lua_function_t::prepared_lua_function_t(lua_State *lua_state,
                                                 calling_context context,
                                                 char const *name, int nresults)
{
    int const index = lua_gettop(lua_state);

    lua_getfield(lua_state, 1, name);

    if (lua_type(lua_state, -1) == LUA_TFUNCTION) {
        m_index = index;
        m_name = name;
        m_nresults = nresults;
        m_calling_context = context;
        return;
    }

    if (lua_type(lua_state, -1) == LUA_TNIL) {
        return;
    }

    throw fmt_error("osm2pgsql.{} must be a function.", name);
}

namespace {

char const *const OSM2PGSQL_OSMOBJECT_CLASS = "osm2pgsql.OSMObject";

void push_osm_object_to_lua_stack(lua_State *lua_state,
                                  osmium::OSMObject const &object)
{
    assert(lua_state);

    /**
     * Table will always have at least 8 fields (id, type, tags, version,
     * timestamp, changeset, uid, user). For ways there are 2 more (is_closed,
     * nodes), for relations 1 more (members).
     */
    constexpr int MAX_TABLE_SIZE = 10;

    lua_createtable(lua_state, 0, MAX_TABLE_SIZE);

    luaX_add_table_int(lua_state, "id", object.id());

    luaX_add_table_str(lua_state, "type",
                       osmium::item_type_to_name(object.type()));

    if (object.version() != 0U) {
        luaX_add_table_int(lua_state, "version", object.version());
    }
    if (object.timestamp().valid()) {
        luaX_add_table_int(lua_state, "timestamp",
                           object.timestamp().seconds_since_epoch());
    }
    if (object.changeset() != 0U) {
        luaX_add_table_int(lua_state, "changeset", object.changeset());
    }
    if (object.uid() != 0U) {
        luaX_add_table_int(lua_state, "uid", object.uid());
    }
    if (object.user()[0] != '\0') {
        luaX_add_table_str(lua_state, "user", object.user());
    }

    if (!object.deleted()) {
        if (object.type() == osmium::item_type::way) {
            auto const &way = static_cast<osmium::Way const &>(object);
            luaX_add_table_bool(lua_state, "is_closed",
                                !way.nodes().empty() && way.is_closed());
            luaX_add_table_array(lua_state, "nodes", way.nodes(),
                                 [&](osmium::NodeRef const &wn) {
                                     lua_pushinteger(lua_state, wn.ref());
                                 });
        } else if (object.type() == osmium::item_type::relation) {
            auto const &relation =
                static_cast<osmium::Relation const &>(object);
            luaX_add_table_array(
                lua_state, "members", relation.members(),
                [&](osmium::RelationMember const &member) {
                    lua_createtable(lua_state, 0, 3);
                    std::array<char, 2> tmp{"x"};
                    tmp[0] = osmium::item_type_to_char(member.type());
                    luaX_add_table_str(lua_state, "type", tmp.data());
                    luaX_add_table_int(lua_state, "ref", member.ref());
                    luaX_add_table_str(lua_state, "role", member.role());
                });
        }

        lua_pushliteral(lua_state, "tags");
        lua_createtable(lua_state, 0, (int)object.tags().size());
        for (auto const &tag : object.tags()) {
            luaX_add_table_str(lua_state, tag.key(), tag.value());
        }
        lua_rawset(lua_state, -3);

        // Set the metatable of this object
        lua_pushstring(lua_state, OSM2PGSQL_OSMOBJECT_CLASS);
        lua_gettable(lua_state, LUA_REGISTRYINDEX);
        lua_setmetatable(lua_state, -2);
    }
}

/**
 * Helper function to push the lon/lat of the specified location onto the
 * Lua stack
 */
void push_location(lua_State *lua_state, osmium::Location location) noexcept
{
    lua_pushnumber(lua_state, location.lon());
    lua_pushnumber(lua_state, location.lat());
}

// Check that the first element on the Lua stack is a "type_name"
// parameter and return its internal index.
std::size_t idx_from_param(lua_State *lua_state, char const *type_name)
{
    assert(lua_gettop(lua_state) >= 1);

    void const *const user_data = lua_touserdata(lua_state, 1);

    if (user_data == nullptr || !lua_getmetatable(lua_state, 1)) {
        throw fmt_error("First parameter must be of type {}.", type_name);
    }

    luaL_getmetatable(lua_state, type_name);
    if (!lua_rawequal(lua_state, -1, -2)) {
        throw fmt_error("First parameter must be of type {}.", type_name);
    }
    lua_pop(lua_state, 2); // remove the two metatables

    return *static_cast<std::size_t const *>(user_data);
}

template <typename CONTAINER>
typename CONTAINER::value_type &get_from_idx_param(lua_State *lua_state,
                                                   CONTAINER *container,
                                                   char const *type_name)
{
    if (lua_gettop(lua_state) < 1) {
        throw fmt_error("Argument #1 has to be of type {}.", type_name);
    }

    auto &item = container->at(idx_from_param(lua_state, type_name));
    lua_remove(lua_state, 1);
    return item;
}

std::size_t get_nodes(middle_query_t const &middle, osmium::Way *way)
{
    constexpr std::size_t MAX_MISSING_NODES = 100;
    static std::size_t count_missing_nodes = 0;

    auto const count = middle.nodes_get_list(&way->nodes());

    if (count_missing_nodes <= MAX_MISSING_NODES &&
        count != way->nodes().size()) {
        util::string_joiner_t id_list{','};
        for (auto const &nr : way->nodes()) {
            if (!nr.location().valid()) {
                id_list.add(fmt::to_string(nr.ref()));
                ++count_missing_nodes;
            }
        }

        log_debug("Missing nodes in way {}: {}", way->id(), id_list());

        if (count_missing_nodes > MAX_MISSING_NODES) {
            log_debug("Reported more than {} missing nodes, no further missing "
                      "nodes will be reported!",
                      MAX_MISSING_NODES);
        }
    }

    return count;
}

void flush_tables(std::vector<table_connection_t> &table_connections)
{
    for (auto &table : table_connections) {
        table.flush();
    }
}

void create_expire_tables(std::vector<expire_output_t> const &expire_outputs,
                          connection_params_t const &connection_params)
{
    if (std::all_of(expire_outputs.cbegin(), expire_outputs.cend(),
                    [](auto const &expire_output) {
                        return expire_output.table().empty();
                    })) {
        return;
    }

    pg_conn_t const connection{connection_params, "out.flex.expire"};
    for (auto const &expire_output : expire_outputs) {
        if (!expire_output.table().empty()) {
            expire_output.create_output_table(connection);
        }
    }
}

void check_for_object(lua_State *lua_state, char const *const function_name)
{
    // This is used to make sure we are printing warnings only once per
    // function name.
    static std::set<std::string> message_shown;
    if (message_shown.count(function_name)) {
        return;
    }

    int const num_params = lua_gettop(lua_state);
    if (num_params == 0) {
        log_warn("You should use the syntax 'object:{}()' (with the colon, not "
                 "a point) to call functions on the OSM object.",
                 function_name);

        message_shown.emplace(function_name);
        return;
    }

    if (lua_getmetatable(lua_state, 1)) {
        luaL_getmetatable(lua_state, OSM2PGSQL_OSMOBJECT_CLASS);
        if (lua_rawequal(lua_state, -1, -2)) {
            lua_pop(lua_state, 2); // remove the two metatables
            return;
        }
        lua_pop(lua_state, 2); // remove the two metatables
    }

    message_shown.emplace(function_name);
    log_warn("First and only parameter for {0}() must be the OSM object. Call "
             "it like this: 'object:{0}()'.",
             function_name);
}

/**
 * Expects a Lua (hash) table on the stack, reads the field with name of the
 * 'type' parameter which must be either nil or a Lua (array) table, in which
 * case all (integer) ids in that table are reads into the 'ids' out
 * parameter.
 */
void get_object_ids(lua_State *lua_state, char const *const type, idlist_t *ids)
{
    lua_getfield(lua_state, -1, type);
    int const ltype = lua_type(lua_state, -1);

    if (ltype == LUA_TNIL) {
        lua_pop(lua_state, 1);
        return;
    }

    if (ltype != LUA_TTABLE) {
        lua_pop(lua_state, 1);
        throw fmt_error(
            "Table returned from select_relation_members() contains '{}' "
            "field, but it isn't an array table.",
            type);
    }

    if (!luaX_is_array(lua_state)) {
        lua_pop(lua_state, 1);
        throw fmt_error(
            "Table returned from select_relation_members() contains '{}' "
            "field, but it isn't an array table.",
            type);
    }

    luaX_for_each(lua_state, [&]() {
        osmid_t const id = lua_tointeger(lua_state, -1);
        if (id == 0) {
            throw fmt_error(
                "Table returned from select_relation_members() contains "
                "'{}' field, which must contain an array of non-zero "
                "integer node ids.",
                type);
        }

        ids->push_back(id);
    });

    lua_pop(lua_state, 1);
}

} // anonymous namespace

/**
 * Helper function checking that Lua function "name" is called in the correct
 * context and without parameters.
 */
void output_flex_t::check_context_and_state(char const *name,
                                            char const *context, bool condition)
{
    check_for_object(lua_state(), name);

    if (condition) {
        throw fmt_error(
            "The function {}() can only be called (directly or indirectly) "
            "from the process_[untagged]_{}() functions.",
            name, context);
    }

    if (lua_gettop(lua_state()) > 1) {
        throw fmt_error("No parameter(s) needed for {}().", name);
    }
}

int output_flex_t::app_get_bbox()
{
    check_context_and_state(
        "get_bbox", "node/way/relation",
        m_calling_context != calling_context::process_node &&
            m_calling_context != calling_context::process_way &&
            m_calling_context != calling_context::process_relation);

    if (m_calling_context == calling_context::process_node) {
        push_location(lua_state(), m_context_node->location());
        push_location(lua_state(), m_context_node->location());
        return 4;
    }

    if (m_calling_context == calling_context::process_way) {
        m_way_cache.add_nodes(middle());
        auto const bbox = m_way_cache.get().envelope();
        if (bbox.valid()) {
            push_location(lua_state(), bbox.bottom_left());
            push_location(lua_state(), bbox.top_right());
            return 4;
        }
        return 0;
    }

    if (m_calling_context == calling_context::process_relation) {
        m_relation_cache.add_members(middle());
        osmium::Box bbox;

        // Bounding boxes of all the member nodes
        for (auto const &node :
             m_relation_cache.members_buffer().select<osmium::Node>()) {
            bbox.extend(node.location());
        }

        // Bounding boxes of all the member ways
        for (auto const &way :
             m_relation_cache.members_buffer().select<osmium::Way>()) {
            bbox.extend(way.nodes().envelope());
        }

        if (bbox.valid()) {
            push_location(lua_state(), bbox.bottom_left());
            push_location(lua_state(), bbox.top_right());
            return 4;
        }
    }

    return 0;
}

int output_flex_t::app_as_point()
{
    check_context_and_state("as_point", "node",
                            m_calling_context != calling_context::process_node);

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_point(geom, *m_context_node);

    return 1;
}

int output_flex_t::app_as_linestring()
{
    check_context_and_state("as_linestring", "way",
                            m_calling_context != calling_context::process_way);

    m_way_cache.add_nodes(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_linestring(geom, m_way_cache.get());

    return 1;
}

int output_flex_t::app_as_polygon()
{
    check_context_and_state("as_polygon", "way",
                            m_calling_context != calling_context::process_way);

    m_way_cache.add_nodes(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_polygon(geom, m_way_cache.get(), &m_area_buffer);

    return 1;
}

int output_flex_t::app_as_multipoint()
{
    check_context_and_state(
        "as_multipoint", "node/relation",
        m_calling_context != calling_context::process_node &&
            m_calling_context != calling_context::process_relation);

    auto *geom = create_lua_geometry_object(lua_state());

    if (m_calling_context == calling_context::process_node) {
        geom::create_point(geom, *m_context_node);
    } else {
        m_relation_cache.add_members(middle());
        geom::create_multipoint(geom, m_relation_cache.members_buffer());
    }

    return 1;
}

int output_flex_t::app_as_multilinestring()
{
    check_context_and_state("as_multilinestring", "way/relation",
                            m_calling_context != calling_context::process_way &&
                                m_calling_context !=
                                    calling_context::process_relation);

    if (m_calling_context == calling_context::process_way) {
        m_way_cache.add_nodes(middle());

        auto *geom = create_lua_geometry_object(lua_state());
        geom::create_linestring(geom, m_way_cache.get());
        return 1;
    }

    m_relation_cache.add_members(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_multilinestring(geom, m_relation_cache.members_buffer(),
                                 false);

    return 1;
}

int output_flex_t::app_as_multipolygon()
{
    check_context_and_state("as_multipolygon", "way/relation",
                            m_calling_context != calling_context::process_way &&
                                m_calling_context !=
                                    calling_context::process_relation);

    if (m_calling_context == calling_context::process_way) {
        m_way_cache.add_nodes(middle());

        auto *geom = create_lua_geometry_object(lua_state());
        geom::create_polygon(geom, m_way_cache.get(), &m_area_buffer);

        return 1;
    }

    m_relation_cache.add_members(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_multipolygon(geom, m_relation_cache.get(),
                              m_relation_cache.members_buffer(),
                              &m_area_buffer);

    return 1;
}

int output_flex_t::app_as_geometrycollection()
{
    check_context_and_state("as_geometrycollection", "relation",
                            m_calling_context !=
                                calling_context::process_relation);

    m_relation_cache.add_members(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_collection(geom, m_relation_cache.members_buffer());

    return 1;
}

int output_flex_t::app_define_locator()
{
    if (m_calling_context != calling_context::main) {
        throw std::runtime_error{
            "Locators have to be defined in the"
            " main Lua code, not in any of the callbacks."};
    }

    return setup_flex_locator(lua_state(), m_locators.get());
}

int output_flex_t::app_define_table()
{
    if (m_calling_context != calling_context::main) {
        throw std::runtime_error{
            "Database tables have to be defined in the"
            " main Lua code, not in any of the callbacks."};
    }

    return setup_flex_table(lua_state(), m_tables.get(), m_expire_outputs.get(),
                            get_options()->dbschema,
                            get_options()->slim && !get_options()->droptemp,
                            get_options()->append);
}

int output_flex_t::app_define_expire_output()
{
    if (m_calling_context != calling_context::main) {
        throw std::runtime_error{
            "Expire outputs have to be defined in the"
            " main Lua code, not in any of the callbacks."};
    }

    return setup_flex_expire_output(lua_state(), get_options()->dbschema,
                                    m_expire_outputs.get());
}

flex_table_t &output_flex_t::get_table_from_param()
{
    return get_from_idx_param(lua_state(), m_tables.get(),
                              OSM2PGSQL_TABLE_CLASS);
}

expire_output_t &output_flex_t::get_expire_output_from_param()
{
    return get_from_idx_param(lua_state(), m_expire_outputs.get(),
                              OSM2PGSQL_EXPIRE_OUTPUT_CLASS);
}

locator_t &output_flex_t::get_locator_from_param()
{
    return get_from_idx_param(lua_state(), m_locators.get(),
                              OSM2PGSQL_LOCATOR_CLASS);
}

bool output_flex_t::way_cache_t::init(middle_query_t const &middle, osmid_t id)
{
    m_buffer.clear();
    m_num_way_nodes = std::numeric_limits<std::size_t>::max();

    if (!middle.way_get(id, &m_buffer)) {
        return false;
    }
    m_way = &m_buffer.get<osmium::Way>(0);
    return true;
}

void output_flex_t::way_cache_t::init(osmium::Way *way)
{
    m_buffer.clear();
    m_num_way_nodes = std::numeric_limits<std::size_t>::max();

    m_way = way;
}

std::size_t output_flex_t::way_cache_t::add_nodes(middle_query_t const &middle)
{
    if (m_num_way_nodes == std::numeric_limits<std::size_t>::max()) {
        m_num_way_nodes = get_nodes(middle, m_way);
    }

    return m_num_way_nodes;
}

bool output_flex_t::relation_cache_t::init(middle_query_t const &middle,
                                           osmid_t id)
{
    m_relation_buffer.clear();
    m_members_buffer.clear();

    if (!middle.relation_get(id, &m_relation_buffer)) {
        return false;
    }
    m_relation = &m_relation_buffer.get<osmium::Relation>(0);
    return true;
}

void output_flex_t::relation_cache_t::init(osmium::Relation const &relation)
{
    m_relation_buffer.clear();
    m_members_buffer.clear();

    m_relation = &relation;
}

bool output_flex_t::relation_cache_t::add_members(middle_query_t const &middle)
{
    if (members_buffer().committed() == 0) {
        auto const num_members = middle.rel_members_get(
            *m_relation, &m_members_buffer,
            osmium::osm_entity_bits::node | osmium::osm_entity_bits::way);

        if (num_members == 0) {
            return false;
        }

        for (auto &node : m_members_buffer.select<osmium::Node>()) {
            if (!node.location().valid()) {
                node.set_location(middle.get_node_location(node.id()));
            }
        }

        for (auto &way : m_members_buffer.select<osmium::Way>()) {
            get_nodes(middle, &way);
        }
    }

    return true;
}

osmium::OSMObject const &
output_flex_t::check_and_get_context_object(flex_table_t const &table)
{
    if (m_calling_context == calling_context::process_node) {
        if (!table.matches_type(osmium::item_type::node)) {
            throw fmt_error("Trying to add node to table '{}'.", table.name());
        }
        return *m_context_node;
    }

    if (m_calling_context == calling_context::process_way) {
        if (!table.matches_type(osmium::item_type::way)) {
            throw fmt_error("Trying to add way to table '{}'.", table.name());
        }
        return m_way_cache.get();
    }

    assert(m_calling_context == calling_context::process_relation);

    if (!table.matches_type(osmium::item_type::relation)) {
        throw fmt_error("Trying to add relation to table '{}'.", table.name());
    }
    return m_relation_cache.get();
}

int output_flex_t::table_insert()
{
    if (m_disable_insert) {
        return 0;
    }

    if (m_calling_context != calling_context::process_node &&
        m_calling_context != calling_context::process_way &&
        m_calling_context != calling_context::process_relation) {
        throw std::runtime_error{
            "The function insert() can only be called from the "
            "process_node/way/relation() functions."};
    }

    auto const num_params = lua_gettop(lua_state());
    if (num_params != 2) {
        throw std::runtime_error{
            "Need two parameters: The osm2pgsql.Table and the row data."};
    }

    // The first parameter is the table object.
    auto &table_connection = m_table_connections.at(
        idx_from_param(lua_state(), OSM2PGSQL_TABLE_CLASS));

    // The second parameter must be a Lua table with the contents for the
    // fields.
    luaL_checktype(lua_state(), 2, LUA_TTABLE);
    lua_remove(lua_state(), 1);

    auto const &table = table_connection.table();
    auto const &object = check_and_get_context_object(table);
    osmid_t const id = table.map_id(object.type(), object.id());

    table_connection.new_line();
    auto *copy_mgr = table_connection.copy_mgr();

    try {
        for (auto const &column : table_connection.table().columns()) {
            if (column.create_only()) {
                continue;
            }
            if (column.type() == table_column_type::id_type) {
                copy_mgr->add_column(type_to_char(object.type()));
            } else if (column.type() == table_column_type::id_num) {
                copy_mgr->add_column(id);
            } else {
                flex_write_column(lua_state(), copy_mgr, column,
                                  &m_expire_tiles);
            }
        }
        table_connection.increment_insert_counter();
    } catch (not_null_exception_t const &e) {
        copy_mgr->rollback_line();
        lua_pushboolean(lua_state(), false);
        lua_pushliteral(lua_state(), "null value in not null column.");
        luaX_pushstring(lua_state(), e.column().name());
        push_osm_object_to_lua_stack(lua_state(), object);
        table_connection.increment_not_null_error_counter();
        return 4;
    }

    copy_mgr->finish_line();

    lua_pushboolean(lua_state(), true);
    return 1;
}

void output_flex_t::call_lua_function(prepared_lua_function_t func)
{
    lua_pushvalue(lua_state(), func.index());
    if (luaX_pcall(lua_state(), 0, func.nresults())) {
        throw fmt_error("Failed to execute Lua function 'osm2pgsql.{}': {}.",
                        func.name(), lua_tostring(lua_state(), -1));
    }
}

void output_flex_t::call_lua_function(prepared_lua_function_t func,
                                      osmium::OSMObject const &object)
{
    m_calling_context = func.context();

    lua_pushvalue(lua_state(), func.index());          // the function to call
    push_osm_object_to_lua_stack(lua_state(), object); // the single argument

    luaX_set_context(lua_state(), this);
    if (luaX_pcall(lua_state(), 1, func.nresults())) {
        throw fmt_error("Failed to execute Lua function 'osm2pgsql.{}': {}.",
                        func.name(), lua_tostring(lua_state(), -1));
    }

    m_calling_context = calling_context::main;
}

void output_flex_t::get_mutex_and_call_lua_function(
    prepared_lua_function_t func)
{
    std::lock_guard<std::mutex> const guard{lua_mutex};
    call_lua_function(func);
}

void output_flex_t::get_mutex_and_call_lua_function(
    prepared_lua_function_t func, osmium::OSMObject const &object)
{
    std::lock_guard<std::mutex> const guard{lua_mutex};
    call_lua_function(func, object);
}

void output_flex_t::pending_way(osmid_t id)
{
    if (!m_process_way && !m_process_untagged_way) {
        return;
    }

    if (!m_way_cache.init(middle(), id)) {
        return;
    }

    way_delete(id);
    auto const &func = m_way_cache.get().tags().empty() ? m_process_untagged_way
                                                        : m_process_way;
    if (func) {
        get_mutex_and_call_lua_function(func, m_way_cache.get());
    }
}

void output_flex_t::select_relation_members()
{
    if (!m_select_relation_members) {
        return;
    }

    // We can not use get_mutex_and_call_lua_function() here, because we need
    // the mutex to stick around as long as we are looking at the Lua stack.
    std::lock_guard<std::mutex> const guard{lua_mutex};
    call_lua_function(m_select_relation_members, m_relation_cache.get());

    // If the function returned nil there is nothing to be marked.
    if (lua_type(lua_state(), -1) == LUA_TNIL) {
        lua_pop(lua_state(), 1); // return value (nil)
        return;
    }

    if (lua_type(lua_state(), -1) != LUA_TTABLE) {
        throw std::runtime_error{"select_relation_members() returned something "
                                 "other than nil or a table."};
    }

    // We have established that we have a table...

    // Get the 'nodes' and 'ways' fields...
    get_object_ids(lua_state(), "nodes", m_stage2_node_ids.get());
    get_object_ids(lua_state(), "ways", m_stage2_way_ids.get());

    lua_pop(lua_state(), 1); // return value (a table)
}

void output_flex_t::select_relation_members(osmid_t id)
{
    if (!m_select_relation_members) {
        return;
    }

    if (!m_relation_cache.init(middle(), id)) {
        return;
    }

    select_relation_members();
}

void output_flex_t::process_relation()
{
    auto const &func = m_relation_cache.get().tags().empty()
                           ? m_process_untagged_relation
                           : m_process_relation;
    if (func) {
        get_mutex_and_call_lua_function(func, m_relation_cache.get());
    }
}

void output_flex_t::pending_relation(osmid_t id)
{
    if (!m_process_relation && !m_process_untagged_relation &&
        !m_select_relation_members) {
        return;
    }

    if (!m_relation_cache.init(middle(), id)) {
        return;
    }

    select_relation_members();
    delete_from_tables(osmium::item_type::relation, id);
    process_relation();
}

void output_flex_t::pending_relation_stage1c(osmid_t id)
{
    if (!m_process_relation && !m_process_untagged_relation) {
        return;
    }

    if (!m_relation_cache.init(middle(), id)) {
        return;
    }

    m_disable_insert = true;
    process_relation();
    m_disable_insert = false;
}

void output_flex_t::sync()
{
    for (auto &table : m_table_connections) {
        table.sync();
    }
}

void output_flex_t::after_nodes()
{
    if (m_after_nodes) {
        get_mutex_and_call_lua_function(m_after_nodes);
    }

    flush_tables(m_table_connections);
}

void output_flex_t::after_ways()
{
    if (m_after_ways) {
        get_mutex_and_call_lua_function(m_after_ways);
    }

    flush_tables(m_table_connections);
}

void output_flex_t::after_relations()
{
    if (m_after_relations) {
        get_mutex_and_call_lua_function(m_after_relations);
    }

    flush_tables(m_table_connections);
}

void output_flex_t::stop()
{
    for (auto &table : m_table_connections) {
        table.task_set(thread_pool().submit([&]() {
            pg_conn_t const db_connection{get_options()->connection_params,
                                          "out.flex.stop"};
            table.stop(db_connection,
                       get_options()->slim && !get_options()->droptemp,
                       get_options()->append);
        }));
    }

    assert(m_expire_outputs->size() == m_expire_tiles.size());
    for (std::size_t i = 0; i < m_expire_outputs->size(); ++i) {
        if (!m_expire_tiles[i].empty()) {
            auto const &eo = (*m_expire_outputs)[i];

            std::size_t const count =
                eo.output(m_expire_tiles[i].get_tiles(),
                          get_options()->connection_params);

            log_info("Wrote {} entries to expire output [{}].", count, i);
        }
    }
}

void output_flex_t::wait()
{
    std::exception_ptr eptr;
    flex_table_t const *table_with_error = nullptr;

    for (auto &table : m_table_connections) {
        try {
            table.task_wait();
        } catch (...) {
            eptr = std::current_exception();
            table_with_error = &table.table();
        }
    }

    if (eptr) {
        log_error("Error while doing postprocessing on table '{}':",
                  table_with_error->name());
        std::rethrow_exception(eptr);
    }
}

void output_flex_t::node_add(osmium::Node const &node)
{
    auto const &func =
        node.tags().empty() ? m_process_untagged_node : m_process_node;

    if (!func) {
        return;
    }

    m_context_node = &node;
    get_mutex_and_call_lua_function(func, node);
    m_context_node = nullptr;
}

void output_flex_t::way_add(osmium::Way *way)
{
    assert(way);

    auto const &func =
        way->tags().empty() ? m_process_untagged_way : m_process_way;

    if (!func) {
        return;
    }

    m_way_cache.init(way);
    get_mutex_and_call_lua_function(func, m_way_cache.get());
}

void output_flex_t::relation_add(osmium::Relation const &relation)
{
    auto const &func = relation.tags().empty() ? m_process_untagged_relation
                                               : m_process_relation;

    if (!func) {
        return;
    }

    m_relation_cache.init(relation);
    select_relation_members();
    get_mutex_and_call_lua_function(func, relation);
}

void output_flex_t::delete_from_table(table_connection_t *table_connection,
                                      pg_conn_t const &db_connection,
                                      osmium::item_type type, osmid_t osm_id)
{
    assert(table_connection);
    auto const id = table_connection->table().map_id(type, osm_id);

    if (table_connection->table().has_columns_with_expire()) {
        auto const result =
            table_connection->get_geoms_by_id(db_connection, type, id);
        auto const num_tuples = result.num_tuples();
        if (num_tuples > 0) {
            int col = 0;
            for (auto const &column : table_connection->table().columns()) {
                if (column.has_expire()) {
                    for (int i = 0; i < num_tuples; ++i) {
                        auto const geom = ewkb_to_geom(result.get(i, col));
                        column.do_expire(geom, &m_expire_tiles);
                    }
                    ++col;
                }
            }
        }
    }

    table_connection->delete_rows_with(type, id);
}

void output_flex_t::delete_from_tables(osmium::item_type type, osmid_t osm_id)
{
    for (auto &table : m_table_connections) {
        if (table.table().matches_type(type) && table.table().has_id_column()) {
            delete_from_table(&table, m_db_connection, type, osm_id);
        }
    }
}

void output_flex_t::node_delete(osmium::Node const &node)
{
    if (m_process_deleted_node) {
        m_context_node = &node;
        get_mutex_and_call_lua_function(m_process_deleted_node, node);
        m_context_node = nullptr;
    }

    node_delete(node.id());
}

void output_flex_t::way_delete(osmium::Way *way)
{
    if (m_process_deleted_way) {
        m_way_cache.init(way);
        get_mutex_and_call_lua_function(m_process_deleted_way,
                                        m_way_cache.get());
    }

    way_delete(way->id());
}

void output_flex_t::relation_delete(osmium::Relation const &rel)
{
    if (m_process_deleted_relation) {
        m_relation_cache.init(rel);
        get_mutex_and_call_lua_function(m_process_deleted_relation, rel);
    }

    relation_delete(rel.id());
}

/* Delete is easy, just remove all traces of this object. We don't need to
 * worry about finding objects that depend on it, since the same diff must
 * contain the change for that also. */
void output_flex_t::node_delete(osmid_t osm_id)
{
    delete_from_tables(osmium::item_type::node, osm_id);
}

void output_flex_t::way_delete(osmid_t osm_id)
{
    delete_from_tables(osmium::item_type::way, osm_id);
}

void output_flex_t::relation_delete(osmid_t osm_id)
{
    select_relation_members(osm_id);
    delete_from_tables(osmium::item_type::relation, osm_id);
}

void output_flex_t::node_modify(osmium::Node const &node)
{
    node_delete(node.id());
    node_add(node);
}

void output_flex_t::way_modify(osmium::Way *way)
{
    way_delete(way->id());
    way_add(way);
}

void output_flex_t::relation_modify(osmium::Relation const &rel)
{
    relation_delete(rel.id());
    relation_add(rel);
}

void output_flex_t::start()
{
    for (auto &table : m_table_connections) {
        table.start(m_db_connection, get_options()->append);
    }

    for (auto &locator : *m_locators) {
        locator.build_index();
    }
}

output_flex_t::output_flex_t(output_flex_t const *other,
                             std::shared_ptr<middle_query_t> mid,
                             std::shared_ptr<db_copy_thread_t> copy_thread)
: output_t(other, std::move(mid)), m_locators(other->m_locators),
  m_tables(other->m_tables), m_expire_outputs(other->m_expire_outputs),
  m_db_connection(get_options()->connection_params, "out.flex.thread"),
  m_stage2_way_ids(other->m_stage2_way_ids),
  m_copy_thread(std::move(copy_thread)), m_lua_state(other->m_lua_state),
  m_area_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_process_node(other->m_process_node), m_process_way(other->m_process_way),
  m_process_relation(other->m_process_relation),
  m_process_untagged_node(other->m_process_untagged_node),
  m_process_untagged_way(other->m_process_untagged_way),
  m_process_untagged_relation(other->m_process_untagged_relation),
  m_process_deleted_node(other->m_process_deleted_node),
  m_process_deleted_way(other->m_process_deleted_way),
  m_process_deleted_relation(other->m_process_deleted_relation),
  m_select_relation_members(other->m_select_relation_members),
  m_after_nodes(other->m_after_nodes), m_after_ways(other->m_after_ways),
  m_after_relations(other->m_after_relations)
{
    for (auto &table : *m_tables) {
        table.prepare(m_db_connection);
        m_table_connections.emplace_back(&table, m_copy_thread);
    }

    for (auto &expire_output : *m_expire_outputs) {
        m_expire_tiles.emplace_back(
            expire_output.maxzoom(),
            reprojection_t::create_projection(PROJ_SPHERE_MERC));
    }
}

std::shared_ptr<output_t>
output_flex_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &copy_thread) const
{
    return std::make_shared<output_flex_t>(this, mid, copy_thread);
}

output_flex_t::output_flex_t(std::shared_ptr<middle_query_t> const &mid,
                             std::shared_ptr<thread_pool_t> thread_pool,
                             options_t const &options,
                             properties_t const &properties)
: output_t(mid, std::move(thread_pool), options),
  m_db_connection(get_options()->connection_params, "out.flex.main"),
  m_copy_thread(std::make_shared<db_copy_thread_t>(options.connection_params)),
  m_area_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    init_lua(options.style, properties);

    // If the osm2pgsql.select_relation_members() Lua function is defined
    // it means we need two-stage processing which in turn means we need
    // the full nodes and ways stored in the middle.
    if (m_select_relation_members) {
        access_requirements().full_nodes = true;
        access_requirements().full_ways = true;
    }

    if (m_tables->empty()) {
        log_warn("No output tables defined!");
    }

    // For backwards compatibility we add a "default" expire output to all
    // tables when the relevant command line options are used.
    if (options.append && options.expire_tiles_zoom) {
        auto &eo = m_expire_outputs->emplace_back();
        eo.set_filename(options.expire_tiles_filename);
        eo.set_minzoom(options.expire_tiles_zoom_min);
        eo.set_maxzoom(options.expire_tiles_zoom);

        for (auto &table : *m_tables) {
            if (table.has_geom_column() &&
                table.geom_column().srid() == PROJ_SPHERE_MERC) {
                expire_config_t config{};
                config.expire_output = m_expire_outputs->size() - 1;
                if (options.expire_tiles_max_bbox > 0.0) {
                    config.mode = expire_mode::hybrid;
                    config.full_area_limit = options.expire_tiles_max_bbox;
                }
                table.geom_column().add_expire(config);
            }
        }
    }

    write_expire_output_list_to_debug_log(*m_expire_outputs);
    write_table_list_to_debug_log(*m_tables);

    for (auto &table : *m_tables) {
        m_table_connections.emplace_back(&table, m_copy_thread);
    }

    for (auto const &expire_output : *m_expire_outputs) {
        m_expire_tiles.emplace_back(
            expire_output.maxzoom(),
            reprojection_t::create_projection(PROJ_SPHERE_MERC));
    }

    create_expire_tables(*m_expire_outputs, get_options()->connection_params);
}

void output_flex_t::init_lua(std::string const &filename,
                             properties_t const &properties)
{
    m_lua_state.reset(luaL_newstate(),
                      [](lua_State *state) { lua_close(state); });

    setup_lua_environment(lua_state(), filename, get_options()->append);

    luaX_add_table_int(lua_state(), "stage", 1);

    lua_pushliteral(lua_state(), "properties");
    lua_createtable(lua_state(), 0, (int)properties.size());
    for (auto const &property : properties) {
        luaX_add_table_str(lua_state(), property.first.c_str(),
                           property.second);
    }
    lua_rawset(lua_state(), -3);

    luaX_add_table_func(lua_state(), "define_locator",
                        lua_trampoline_app_define_locator);

    luaX_add_table_func(lua_state(), "define_table",
                        lua_trampoline_app_define_table);

    luaX_add_table_func(lua_state(), "define_expire_output",
                        lua_trampoline_app_define_expire_output);

    lua_wrapper_expire_output::init(lua_state());
    lua_wrapper_locator::init(lua_state(), get_options()->connection_params);
    lua_wrapper_table::init(lua_state());

    // Clean up stack
    lua_settop(lua_state(), 0);

    init_geometry_class(lua_state());

    assert(lua_gettop(lua_state()) == 0);

    luaX_set_up_metatable(
        lua_state(), "OSMObject", OSM2PGSQL_OSMOBJECT_CLASS,
        {{"get_bbox", lua_trampoline_app_get_bbox},
         {"as_linestring", lua_trampoline_app_as_linestring},
         {"as_point", lua_trampoline_app_as_point},
         {"as_polygon", lua_trampoline_app_as_polygon},
         {"as_multipoint", lua_trampoline_app_as_multipoint},
         {"as_multilinestring", lua_trampoline_app_as_multilinestring},
         {"as_multipolygon", lua_trampoline_app_as_multipolygon},
         {"as_geometrycollection", lua_trampoline_app_as_geometrycollection}});

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

    // Check whether the process_* functions are available and store them on
    // the Lua stack for fast access later
    lua_getglobal(lua_state(), "osm2pgsql");

    m_process_node = prepared_lua_function_t{
        lua_state(), calling_context::process_node, "process_node"};
    m_process_way = prepared_lua_function_t{
        lua_state(), calling_context::process_way, "process_way"};
    m_process_relation = prepared_lua_function_t{
        lua_state(), calling_context::process_relation, "process_relation"};

    m_process_untagged_node = prepared_lua_function_t{
        lua_state(), calling_context::process_node, "process_untagged_node"};
    m_process_untagged_way = prepared_lua_function_t{
        lua_state(), calling_context::process_way, "process_untagged_way"};
    m_process_untagged_relation =
        prepared_lua_function_t{lua_state(), calling_context::process_relation,
                                "process_untagged_relation"};

    m_process_deleted_node = prepared_lua_function_t{
        lua_state(), calling_context::process_node, "process_deleted_node"};
    m_process_deleted_way = prepared_lua_function_t{
        lua_state(), calling_context::process_way, "process_deleted_way"};
    m_process_deleted_relation =
        prepared_lua_function_t{lua_state(), calling_context::process_relation,
                                "process_deleted_relation"};

    m_select_relation_members = prepared_lua_function_t{
        lua_state(), calling_context::select_relation_members,
        "select_relation_members", 1};

    m_after_nodes = prepared_lua_function_t{lua_state(), calling_context::main,
                                            "after_nodes"};
    m_after_ways = prepared_lua_function_t{lua_state(), calling_context::main,
                                           "after_ways"};
    m_after_relations = prepared_lua_function_t{
        lua_state(), calling_context::main, "after_relations"};

    lua_remove(lua_state(), 1); // global "osm2pgsql"
}

idlist_t const &output_flex_t::get_marked_node_ids()
{
    if (m_stage2_node_ids->empty()) {
        log_info("Skipping stage 1c for nodes (no marked nodes).");
    } else {
        log_info("Entering stage 1c processing of {} nodes...",
                 m_stage2_node_ids->size());
        m_stage2_node_ids->sort_unique();
    }

    return *m_stage2_node_ids;
}

idlist_t const &output_flex_t::get_marked_way_ids()
{
    if (m_stage2_way_ids->empty()) {
        log_info("Skipping stage 1c for ways (no marked ways).");
    } else {
        log_info("Entering stage 1c processing of {} ways...",
                 m_stage2_way_ids->size());
        m_stage2_way_ids->sort_unique();
    }

    return *m_stage2_way_ids;
}

void output_flex_t::reprocess_marked()
{
    if (m_stage2_node_ids->empty() && m_stage2_way_ids->empty()) {
        log_info("No marked nodes or ways (Skipping stage 2).");
        return;
    }

    log_info("Reprocess marked nodes/ways (stage 2)...");

    if (!get_options()->append) {
        util::timer_t timer;

        for (auto &table : m_table_connections) {
            if (table.table().matches_type(osmium::item_type::way) &&
                table.table().has_id_column()) {
                table.table().analyze(m_db_connection);
                table.create_id_index(m_db_connection);
            }
        }

        log_info("Creating id indexes took {}",
                 util::human_readable_duration(timer.stop()));
    }

    lua_gc(lua_state(), LUA_GCCOLLECT, 0);
    log_debug("Lua program uses {} MBytes",
              lua_gc(lua_state(), LUA_GCCOUNT, 0) / 1024);

    lua_getglobal(lua_state(), "osm2pgsql");
    lua_pushinteger(lua_state(), 2);
    lua_setfield(lua_state(), -2, "stage");
    lua_pop(lua_state(), 1); // osm2pgsql

    m_stage2_node_ids->sort_unique();
    m_stage2_way_ids->sort_unique();

    log_info("There are {} nodes to reprocess...", m_stage2_node_ids->size());
    {
        osmium::memory::Buffer node_buffer{
            1024, osmium::memory::Buffer::auto_grow::yes};

        for (osmid_t const id : *m_stage2_node_ids) {
            if (middle().node_get(id, &node_buffer)) {
                node_delete(id);
                if (m_process_node) {
                    auto const &node = node_buffer.get<osmium::Node>(0);
                    m_context_node = &node;
                    get_mutex_and_call_lua_function(m_process_node, node);
                }
            }
            node_buffer.clear();
        }
    }

    // We don't need these any more so can free the memory.
    m_stage2_node_ids->clear();

    log_info("There are {} ways to reprocess...", m_stage2_way_ids->size());

    for (osmid_t const id : *m_stage2_way_ids) {
        if (!m_way_cache.init(middle(), id)) {
            continue;
        }
        way_delete(id);
        if (m_process_way) {
            get_mutex_and_call_lua_function(m_process_way, m_way_cache.get());
        }
    }

    // We don't need these any more so can free the memory.
    m_stage2_way_ids->clear();
}

void output_flex_t::merge_expire_trees(output_t *other)
{
    auto *const opgsql = dynamic_cast<output_flex_t *>(other);
    assert(m_expire_tiles.size() == opgsql->m_expire_tiles.size());
    for (std::size_t i = 0; i < m_expire_tiles.size(); ++i) {
        m_expire_tiles[i].merge_and_destroy(&opgsql->m_expire_tiles[i]);
    }
}
