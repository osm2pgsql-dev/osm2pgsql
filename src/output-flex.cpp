/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-copy.hpp"
#include "debug-output.hpp"
#include "expire-output.hpp"
#include "expire-tiles.hpp"
#include "flex-index.hpp"
#include "flex-lua-geom.hpp"
#include "flex-lua-index.hpp"
#include "flex-lua-table.hpp"
#include "flex-lua-expire-output.hpp"
#include "flex-write.hpp"
#include "format.hpp"
#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-transform.hpp"
#include "logging.hpp"
#include "lua-init.hpp"
#include "lua-setup.hpp"
#include "lua-utils.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-flex.hpp"
#include "pgsql.hpp"
#include "pgsql-capabilities.hpp"
#include "reprojection.hpp"
#include "thread-pool.hpp"
#include "util.hpp"
#include "version.hpp"
#include "wkb.hpp"

#include <osmium/osm/types_from_string.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

// Mutex used to coordinate access to Lua code
static std::mutex lua_mutex;

// Lua can't call functions on C++ objects directly. This macro defines simple
// C "trampoline" functions which are called from Lua which get the current
// context (the output_flex_t object) and call the respective function on the
// context object.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define TRAMPOLINE(func_name, lua_name)                                        \
    static int lua_trampoline_##func_name(lua_State *lua_state)                \
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

TRAMPOLINE(table_name, name)
TRAMPOLINE(table_schema, schema)
TRAMPOLINE(table_cluster, cluster)
TRAMPOLINE(table_add_row, add_row)
TRAMPOLINE(table_insert, insert)
TRAMPOLINE(table_columns, columns)
TRAMPOLINE(table_tostring, __tostring)

TRAMPOLINE(expire_output_name, name)
TRAMPOLINE(expire_output_minzoom, minzoom)
TRAMPOLINE(expire_output_maxzoom, maxzoom)
TRAMPOLINE(expire_output_filename, filename)
TRAMPOLINE(expire_output_schema, schema)
TRAMPOLINE(expire_output_table, table)
TRAMPOLINE(expire_output_tostring, __tostring)

static char const *const osm2pgsql_object_metatable =
    "osm2pgsql.object_metatable";

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

static void push_osm_object_to_lua_stack(lua_State *lua_state,
                                         osmium::OSMObject const &object,
                                         bool with_attributes)
{
    assert(lua_state);

    /**
     * Table will always have at least 3 fields (id, type, tags). And 5 more if
     * with_attributes is true (version, timestamp, changeset, uid, user). For
     * ways there are 2 more (is_closed, nodes), for relations 1 more (members).
     */
    constexpr int const max_table_size = 10;

    lua_createtable(lua_state, 0, max_table_size);

    luaX_add_table_int(lua_state, "id", object.id());

    luaX_add_table_str(lua_state, "type",
                       osmium::item_type_to_name(object.type()));

    if (with_attributes) {
        if (object.version() != 0U) {
            luaX_add_table_int(lua_state, "version", object.version());
        } else {
            // This is a workaround, because the middle will give us the
            // attributes as pseudo-tags.
            char const *const val = object.tags()["osm_version"];
            if (val) {
                luaX_add_table_int(lua_state, "version",
                                   osmium::string_to_object_version(val));
            }
        }

        if (object.timestamp().valid()) {
            luaX_add_table_int(lua_state, "timestamp",
                               object.timestamp().seconds_since_epoch());
        } else {
            // This is a workaround, because the middle will give us the
            // attributes as pseudo-tags.
            char const *const val = object.tags()["osm_timestamp"];
            if (val) {
                auto const timestamp = osmium::Timestamp{val};
                luaX_add_table_int(lua_state, "timestamp",
                                   timestamp.seconds_since_epoch());
            }
        }

        if (object.changeset() != 0U) {
            luaX_add_table_int(lua_state, "changeset", object.changeset());
        } else {
            char const *const val = object.tags()["osm_changeset"];
            // This is a workaround, because the middle will give us the
            // attributes as pseudo-tags.
            if (val) {
                luaX_add_table_int(lua_state, "changeset",
                                   osmium::string_to_changeset_id(val));
            }
        }

        if (object.uid() != 0U) {
            luaX_add_table_int(lua_state, "uid", object.uid());
        } else {
            // This is a workaround, because the middle will give us the
            // attributes as pseudo-tags.
            char const *const val = object.tags()["osm_uid"];
            if (val) {
                luaX_add_table_int(lua_state, "uid",
                                   osmium::string_to_uid(val));
            }
        }

        if (object.user()[0] != '\0') {
            luaX_add_table_str(lua_state, "user", object.user());
        } else {
            // This is a workaround, because the middle will give us the
            // attributes as pseudo-tags.
            char const *const val = object.tags()["osm_user"];
            if (val) {
                luaX_add_table_str(lua_state, "user", val);
            }
        }
    }

    if (object.type() == osmium::item_type::way) {
        auto const &way = static_cast<osmium::Way const &>(object);
        luaX_add_table_bool(lua_state, "is_closed",
                            !way.nodes().empty() && way.is_closed());
        luaX_add_table_array(lua_state, "nodes", way.nodes(),
                             [&](osmium::NodeRef const &wn) {
                                 lua_pushinteger(lua_state, wn.ref());
                             });
    } else if (object.type() == osmium::item_type::relation) {
        auto const &relation = static_cast<osmium::Relation const &>(object);
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
    lua_pushlightuserdata(lua_state, (void *)osm2pgsql_object_metatable);
    lua_gettable(lua_state, LUA_REGISTRYINDEX);
    lua_setmetatable(lua_state, -2);
}

/**
 * Helper function to push the lon/lat of the specified location onto the
 * Lua stack
 */
static void push_location(lua_State *lua_state,
                          osmium::Location location) noexcept
{
    lua_pushnumber(lua_state, location.lon());
    lua_pushnumber(lua_state, location.lat());
}

/**
 * Helper function checking that Lua function "name" is called in the correct
 * context and without parameters.
 */
void output_flex_t::check_context_and_state(char const *name,
                                            char const *context, bool condition)
{
    if (condition) {
        throw fmt_error("The function {}() can only be called from the {}.",
                        name, context);
    }

    if (lua_gettop(lua_state()) > 1) {
        throw fmt_error("No parameter(s) needed for {}().", name);
    }
}

int output_flex_t::app_get_bbox()
{
    check_context_and_state(
        "get_bbox", "process_node/way/relation() functions",
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
        for (auto const &wnl :
             m_relation_cache.members_buffer().select<osmium::WayNodeList>()) {
            bbox.extend(wnl.envelope());
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
    check_context_and_state("as_point", "process_node() function",
                            m_calling_context != calling_context::process_node);

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_point(geom, *m_context_node);

    return 1;
}

int output_flex_t::app_as_linestring()
{
    check_context_and_state("as_linestring", "process_way() function",
                            m_calling_context != calling_context::process_way);

    m_way_cache.add_nodes(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_linestring(geom, m_way_cache.get());

    return 1;
}

int output_flex_t::app_as_polygon()
{
    check_context_and_state("as_polygon", "process_way() function",
                            m_calling_context != calling_context::process_way);

    m_way_cache.add_nodes(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_polygon(geom, m_way_cache.get());

    return 1;
}

int output_flex_t::app_as_multipoint()
{
    check_context_and_state(
        "as_multipoint", "process_node/relation() functions",
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
    check_context_and_state(
        "as_multilinestring", "process_way/relation() functions",
        m_calling_context != calling_context::process_way &&
            m_calling_context != calling_context::process_relation);

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
    check_context_and_state(
        "as_multipolygon", "process_way/relation() functions",
        m_calling_context != calling_context::process_way &&
            m_calling_context != calling_context::process_relation);

    if (m_calling_context == calling_context::process_way) {
        m_way_cache.add_nodes(middle());

        auto *geom = create_lua_geometry_object(lua_state());
        geom::create_polygon(geom, m_way_cache.get());

        return 1;
    }

    m_relation_cache.add_members(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_multipolygon(geom, m_relation_cache.get(),
                              m_relation_cache.members_buffer());

    return 1;
}

int output_flex_t::app_as_geometrycollection()
{
    check_context_and_state(
        "as_geometrycollection", "process_relation() function",
        m_calling_context != calling_context::process_relation);

    m_relation_cache.add_members(middle());

    auto *geom = create_lua_geometry_object(lua_state());
    geom::create_collection(geom, m_relation_cache.members_buffer());

    return 1;
}

int output_flex_t::app_define_table()
{
    if (m_calling_context != calling_context::main) {
        throw std::runtime_error{
            "Database tables have to be defined in the"
            " main Lua code, not in any of the callbacks."};
    }

    return setup_flex_table(lua_state(), m_tables.get(), m_expire_outputs.get(),
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

    return setup_flex_expire_output(lua_state(), m_expire_outputs.get());
}

// Check that the first element on the Lua stack is a "type_name"
// parameter and return its internal index.
static std::size_t idx_from_param(lua_State *lua_state, char const *type_name)
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
static typename CONTAINER::value_type const &
get_from_idx_param(lua_State *lua_state, CONTAINER *container,
                   char const *type_name)
{
    if (lua_gettop(lua_state) != 1) {
        throw fmt_error("Need exactly one parameter of type {}.", type_name);
    }

    auto const &item = container->at(idx_from_param(lua_state, type_name));
    lua_remove(lua_state, 1);
    return item;
}

flex_table_t const &output_flex_t::get_table_from_param()
{
    return get_from_idx_param(lua_state(), m_tables.get(),
                              osm2pgsql_table_name);
}

expire_output_t const &output_flex_t::get_expire_output_from_param()
{
    return get_from_idx_param(lua_state(), m_expire_outputs.get(),
                              osm2pgsql_expire_output_name);
}

int output_flex_t::table_tostring()
{
    auto const &table = get_table_from_param();

    std::string const str{fmt::format("osm2pgsql.Table[{}]", table.name())};
    lua_pushstring(lua_state(), str.c_str());

    return 1;
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
        m_num_way_nodes = middle.nodes_get_list(&m_way->nodes());
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
            middle.nodes_get_list(&(way.nodes()));
        }
    }

    return true;
}

int output_flex_t::table_add_row()
{
    if (m_disable_add_row) {
        return 0;
    }

    if (m_calling_context != calling_context::process_node &&
        m_calling_context != calling_context::process_way &&
        m_calling_context != calling_context::process_relation) {
        throw std::runtime_error{
            "The function add_row() can only be called from the "
            "process_node/way/relation() functions."};
    }

    // Params are the table object and an optional Lua table with the contents
    // for the fields.
    auto const num_params = lua_gettop(lua_state());
    if (num_params < 1 || num_params > 2) {
        throw std::runtime_error{
            "Need two parameters: The osm2pgsql.Table and the row data."};
    }

    auto &table_connection = m_table_connections.at(
        idx_from_param(lua_state(), osm2pgsql_table_name));

    auto const &table = table_connection.table();

    // If there is a second parameter, it must be a Lua table.
    if (num_params == 2) {
        luaL_checktype(lua_state(), 2, LUA_TTABLE);
    }
    lua_remove(lua_state(), 1);

    if (m_calling_context == calling_context::process_node) {
        if (!table.matches_type(osmium::item_type::node)) {
            throw fmt_error("Trying to add node to table '{}'.", table.name());
        }
        add_row(&table_connection, *m_context_node);
    } else if (m_calling_context == calling_context::process_way) {
        if (!table.matches_type(osmium::item_type::way)) {
            throw fmt_error("Trying to add way to table '{}'.", table.name());
        }
        add_row(&table_connection, m_way_cache.get());
    } else if (m_calling_context == calling_context::process_relation) {
        if (!table.matches_type(osmium::item_type::relation)) {
            throw fmt_error("Trying to add relation to table '{}'.",
                            table.name());
        }
        add_row(&table_connection, m_relation_cache.get());
    }

    return 0;
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
    if (m_disable_add_row) {
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
        idx_from_param(lua_state(), osm2pgsql_table_name));

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
        for (auto const &column : table_connection.table()) {
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
    } catch (not_null_exception const &e) {
        copy_mgr->rollback_line();
        lua_pushboolean(lua_state(), false);
        lua_pushstring(lua_state(), "null value in not null column.");
        lua_pushstring(lua_state(), e.column().name().c_str());
        push_osm_object_to_lua_stack(lua_state(), object,
                                     get_options()->extra_attributes);
        table_connection.increment_not_null_error_counter();
        return 4;
    }

    copy_mgr->finish_line();

    lua_pushboolean(lua_state(), true);
    return 1;
}

int output_flex_t::table_columns()
{
    auto const &table = get_table_from_param();

    lua_createtable(lua_state(), (int)table.num_columns(), 0);

    int n = 0;
    for (auto const &column : table) {
        lua_pushinteger(lua_state(), ++n);
        lua_newtable(lua_state());

        luaX_add_table_str(lua_state(), "name", column.name().c_str());
        luaX_add_table_str(lua_state(), "type", column.type_name().c_str());
        luaX_add_table_str(lua_state(), "sql_type",
                           column.sql_type_name().c_str());
        luaX_add_table_str(lua_state(), "sql_modifiers",
                           column.sql_modifiers().c_str());
        luaX_add_table_bool(lua_state(), "not_null", column.not_null());
        luaX_add_table_bool(lua_state(), "create_only", column.create_only());

        lua_rawset(lua_state(), -3);
    }
    return 1;
}

int output_flex_t::table_name()
{
    auto const &table = get_table_from_param();
    lua_pushstring(lua_state(), table.name().c_str());
    return 1;
}

int output_flex_t::table_schema()
{
    auto const &table = get_table_from_param();
    lua_pushstring(lua_state(), table.schema().c_str());
    return 1;
}

int output_flex_t::table_cluster()
{
    auto const &table = get_table_from_param();
    lua_pushboolean(lua_state(), table.cluster_by_geom());
    return 1;
}

geom::geometry_t output_flex_t::run_transform(reprojection const &proj,
                                              geom_transform_t const *transform,
                                              osmium::Node const &node)
{
    return transform->convert(proj, node);
}

geom::geometry_t output_flex_t::run_transform(reprojection const &proj,
                                              geom_transform_t const *transform,
                                              osmium::Way const & /*way*/)
{
    if (m_way_cache.add_nodes(middle()) <= 1U) {
        return {};
    }

    return transform->convert(proj, m_way_cache.get());
}

geom::geometry_t output_flex_t::run_transform(reprojection const &proj,
                                              geom_transform_t const *transform,
                                              osmium::Relation const &relation)
{
    if (!m_relation_cache.add_members(middle())) {
        return {};
    }

    return transform->convert(proj, relation,
                              m_relation_cache.members_buffer());
}

template <typename OBJECT>
void output_flex_t::add_row(table_connection_t *table_connection,
                            OBJECT const &object)
{
    assert(table_connection);
    auto const &table = table_connection->table();

    if (table.has_multiple_geom_columns()) {
        throw fmt_error("Table '{}' has more than one geometry column."
                        " This is not allowed with 'add_row()'."
                        " Maybe use 'insert()' instead?",
                        table.name());
    }

    osmid_t const id = table.map_id(object.type(), object.id());

    if (!table.has_geom_column()) {
        flex_write_row(lua_state(), table_connection, object.type(), id, {}, 0,
                       &m_expire_tiles);
        return;
    }

    // From here we are handling the case where the table has a geometry
    // column. In this case the second parameter to the Lua function add_row()
    // must be present.
    if (lua_gettop(lua_state()) == 0) {
        throw std::runtime_error{
            "Need two parameters: The osm2pgsql.Table and the row data."};
    }

    auto const &proj = table_connection->proj();
    auto const type = table.geom_column().type();

    auto const geom_transform = get_transform(lua_state(), table.geom_column());
    assert(lua_gettop(lua_state()) == 1);

    geom_transform_t const *transform = geom_transform.get();
    if (!transform) {
        transform = get_default_transform(table.geom_column(), object.type());
    }

    // The geometry returned by run_transform() is in 4326 if it is a
    // (multi)polygon. If it is a point or linestring, it is already in the
    // target geometry.
    auto geom = run_transform(proj, transform, object);

    // We need to split a multi geometry into its parts if the geometry
    // column can only take non-multi geometries or if the transform
    // explicitly asked us to split, which is the case when an area
    // transform explicitly set `split_at = 'multi'`.
    bool const split_multi = type == table_column_type::linestring ||
                             type == table_column_type::polygon ||
                             transform->split();

    auto const geoms = geom::split_multi(std::move(geom), split_multi);
    for (auto const &sgeom : geoms) {
        table.geom_column().do_expire(sgeom, &m_expire_tiles);
        flex_write_row(lua_state(), table_connection, object.type(), id, sgeom,
                       table.geom_column().srid(), &m_expire_tiles);
    }
}

int output_flex_t::expire_output_tostring()
{
    auto const &expire_output = get_expire_output_from_param();

    std::string const str{
        fmt::format("osm2pgsql.ExpireOutput[{}]", expire_output.name())};
    lua_pushstring(lua_state(), str.c_str());

    return 1;
}

int output_flex_t::expire_output_name()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushstring(lua_state(), expire_output.name().c_str());
    return 1;
}

int output_flex_t::expire_output_minzoom()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushinteger(lua_state(), expire_output.minzoom());
    return 1;
}

int output_flex_t::expire_output_maxzoom()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushinteger(lua_state(), expire_output.maxzoom());
    return 1;
}

int output_flex_t::expire_output_filename()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushstring(lua_state(), expire_output.filename().c_str());
    return 1;
}

int output_flex_t::expire_output_schema()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushstring(lua_state(), expire_output.schema().c_str());
    return 1;
}

int output_flex_t::expire_output_table()
{
    auto const &expire_output = get_expire_output_from_param();

    lua_pushstring(lua_state(), expire_output.table().c_str());
    return 1;
}

void output_flex_t::call_lua_function(prepared_lua_function_t func,
                                      osmium::OSMObject const &object)
{
    m_calling_context = func.context();

    lua_pushvalue(lua_state(), func.index()); // the function to call
    push_osm_object_to_lua_stack(
        lua_state(), object,
        get_options()->extra_attributes); // the single argument

    luaX_set_context(lua_state(), this);
    if (luaX_pcall(lua_state(), 1, func.nresults())) {
        throw fmt_error("Failed to execute Lua function 'osm2pgsql.{}': {}.",
                        func.name(), lua_tostring(lua_state(), -1));
    }

    m_calling_context = calling_context::main;
}

void output_flex_t::get_mutex_and_call_lua_function(
    prepared_lua_function_t func, osmium::OSMObject const &object)
{
    std::lock_guard<std::mutex> const guard{lua_mutex};
    call_lua_function(func, object);
}

void output_flex_t::pending_way(osmid_t id)
{
    if (!m_process_way) {
        return;
    }

    if (!m_way_cache.init(middle(), id)) {
        return;
    }

    way_delete(id);

    get_mutex_and_call_lua_function(m_process_way, m_way_cache.get());
}

void output_flex_t::select_relation_members()
{
    if (!m_select_relation_members) {
        return;
    }

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

    // We have established that we have a table. Get the 'ways' field...
    lua_getfield(lua_state(), -1, "ways");
    int const ltype = lua_type(lua_state(), -1);

    // No 'ways' field, that is okay, nothing to be marked.
    if (ltype == LUA_TNIL) {
        lua_pop(lua_state(), 2); // return value (a table), ways field (nil)
        return;
    }

    if (ltype != LUA_TTABLE) {
        throw std::runtime_error{
            "Table returned from select_relation_members() contains 'ways' "
            "field, but it isn't an array table."};
    }

    // Iterate over the 'ways' table to get all ids...
    if (!luaX_is_array(lua_state())) {
        throw std::runtime_error{
            "Table returned from select_relation_members() contains 'ways' "
            "field, but it isn't an array table."};
    }

    luaX_for_each(
        lua_state(), [&]() {
            osmid_t const id = lua_tointeger(lua_state(), -1);
            if (id == 0) {
                throw std::runtime_error{
                    "Table returned from select_relation_members() contains "
                    "'ways' field, which must contain an array of non-zero "
                    "integer way ids."};
            }

            m_stage2_way_ids->set(id);
        });

    lua_pop(lua_state(), 2); // return value (a table), ways field (a table)
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

void output_flex_t::pending_relation(osmid_t id)
{
    if (!m_process_relation && !m_select_relation_members) {
        return;
    }

    if (!m_relation_cache.init(middle(), id)) {
        return;
    }

    select_relation_members();
    delete_from_tables(osmium::item_type::relation, id);

    if (m_process_relation) {
        get_mutex_and_call_lua_function(m_process_relation,
                                        m_relation_cache.get());
    }
}

void output_flex_t::pending_relation_stage1c(osmid_t id)
{
    if (!m_process_relation) {
        return;
    }

    if (!m_relation_cache.init(middle(), id)) {
        return;
    }

    m_disable_add_row = true;
    get_mutex_and_call_lua_function(m_process_relation, m_relation_cache.get());
    m_disable_add_row = false;
}

void output_flex_t::sync()
{
    for (auto &table : m_table_connections) {
        table.sync();
    }
}

void output_flex_t::after_nodes()
{
    for (auto &table : m_table_connections) {
        table.flush();
    }
}

void output_flex_t::after_ways()
{
    for (auto &table : m_table_connections) {
        table.flush();
    }
}

void output_flex_t::stop()
{
    for (auto &table : m_table_connections) {
        table.task_set(thread_pool().submit([&]() {
            table.stop(get_options()->slim && !get_options()->droptemp,
                       get_options()->append);
        }));
    }

    assert(m_expire_outputs->size() == m_expire_tiles.size());
    for (std::size_t i = 0; i < m_expire_outputs->size(); ++i) {
        if (!m_expire_tiles[i].empty()) {
            auto const &eo = (*m_expire_outputs)[i];

            std::size_t const count = eo.output(m_expire_tiles[i].get_tiles(),
                                                get_options()->conninfo);

            log_info("Wrote {} entries to expired output '{}'.", count,
                     eo.name());
        }
    }
}

void output_flex_t::wait()
{
    for (auto &table : m_table_connections) {
        table.task_wait();
    }
}

void output_flex_t::node_add(osmium::Node const &node)
{
    if (!m_process_node) {
        return;
    }

    m_context_node = &node;
    get_mutex_and_call_lua_function(m_process_node, node);
    m_context_node = nullptr;
}

void output_flex_t::way_add(osmium::Way *way)
{
    assert(way);

    if (!m_process_way) {
        return;
    }

    m_way_cache.init(way);
    get_mutex_and_call_lua_function(m_process_way, m_way_cache.get());
}

void output_flex_t::relation_add(osmium::Relation const &relation)
{
    if (!m_process_relation) {
        return;
    }

    m_relation_cache.init(relation);
    select_relation_members();
    get_mutex_and_call_lua_function(m_process_relation, relation);
}

void output_flex_t::delete_from_table(table_connection_t *table_connection,
                                      osmium::item_type type, osmid_t osm_id)
{
    assert(table_connection);
    auto const id = table_connection->table().map_id(type, osm_id);

    if (table_connection->table().has_columns_with_expire()) {
        auto const result = table_connection->get_geoms_by_id(type, id);
        auto const num_tuples = result.num_tuples();
        if (num_tuples > 0) {
            int col = 0;
            for (auto const &column : table_connection->table()) {
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
            delete_from_table(&table, type, osm_id);
        }
    }
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
        table.connect(get_options()->conninfo);
        table.start(get_options()->append);
    }
}

static void
create_expire_tables(std::vector<expire_output_t> const &expire_outputs,
                     std::string const &conninfo)
{
    if (std::all_of(expire_outputs.begin(), expire_outputs.end(),
                    [](auto const &expire_output) {
                        return expire_output.table().empty();
                    })) {
        return;
    }

    pg_conn_t connection{conninfo};
    connection.exec("SET client_min_messages = WARNING");
    for (auto &expire_output : expire_outputs) {
        if (!expire_output.table().empty()) {
            expire_output.create_output_table(connection);
        }
    }
}

output_flex_t::output_flex_t(output_flex_t const *other,
                             std::shared_ptr<middle_query_t> mid,
                             std::shared_ptr<db_copy_thread_t> copy_thread)
: output_t(other, std::move(mid)), m_tables(other->m_tables),
  m_expire_outputs(other->m_expire_outputs),
  m_stage2_way_ids(other->m_stage2_way_ids),
  m_copy_thread(std::move(copy_thread)), m_lua_state(other->m_lua_state),
  m_process_node(other->m_process_node), m_process_way(other->m_process_way),
  m_process_relation(other->m_process_relation),
  m_select_relation_members(other->m_select_relation_members)
{
    for (auto &table : *m_tables) {
        auto &tc = m_table_connections.emplace_back(&table, m_copy_thread);
        tc.connect(get_options()->conninfo);
        tc.prepare();
    }

    for (auto &expire_output : *m_expire_outputs) {
        m_expire_tiles.emplace_back(expire_output.maxzoom(),
                                    reprojection::create_projection(3857));
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
                             options_t const &options)
: output_t(mid, std::move(thread_pool), options),
  m_copy_thread(std::make_shared<db_copy_thread_t>(options.conninfo))
{
    init_lua(options.style);

    // If the osm2pgsql.select_relation_members() Lua function is defined
    // it means we need two-stage processing which in turn means we need
    // the full ways stored in the middle.
    if (m_select_relation_members) {
        access_requirements().full_ways = true;
    }

    if (m_tables->empty()) {
        throw std::runtime_error{
            "No tables defined in Lua config. Nothing to do!"};
    }

    // For backwards compatibility we add a "default" expire output (with
    // empty name) when the relevant command line options are used.
    if (options.append && options.expire_tiles_zoom) {
        auto &eo = m_expire_outputs->emplace_back();
        eo.set_filename(options.expire_tiles_filename);
        eo.set_minzoom(options.expire_tiles_zoom_min);
        eo.set_maxzoom(options.expire_tiles_zoom);

        for (auto &table : *m_tables) {
            if (table.has_geom_column() && table.geom_column().srid() == 3857) {
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
    write_table_list_to_debug_log(*m_tables, *m_expire_outputs);

    for (auto &table : *m_tables) {
        m_table_connections.emplace_back(&table, m_copy_thread);
    }

    for (auto &expire_output : *m_expire_outputs) {
        m_expire_tiles.emplace_back(expire_output.maxzoom(),
                                    reprojection::create_projection(3857));
    }

    create_expire_tables(*m_expire_outputs, get_options()->conninfo);
}

/**
 * Define the osm2pgsql.Table class/metatable.
 */
static void init_table_class(lua_State *lua_state)
{
    lua_getglobal(lua_state, "osm2pgsql");
    if (luaL_newmetatable(lua_state, osm2pgsql_table_name) != 1) {
        throw std::runtime_error{"Internal error: Lua newmetatable failed."};
    }
    lua_pushvalue(lua_state, -1); // Copy of new metatable

    // Add metatable as osm2pgsql.Table so we can access it from Lua
    lua_setfield(lua_state, -3, "Table");

    // Now add functions to metatable
    lua_pushvalue(lua_state, -1);
    lua_setfield(lua_state, -2, "__index");
    luaX_add_table_func(lua_state, "__tostring", lua_trampoline_table_tostring);
    luaX_add_table_func(lua_state, "add_row", lua_trampoline_table_add_row);
    luaX_add_table_func(lua_state, "insert", lua_trampoline_table_insert);
    luaX_add_table_func(lua_state, "name", lua_trampoline_table_name);
    luaX_add_table_func(lua_state, "schema", lua_trampoline_table_schema);
    luaX_add_table_func(lua_state, "cluster", lua_trampoline_table_cluster);
    luaX_add_table_func(lua_state, "columns", lua_trampoline_table_columns);

    lua_pop(lua_state, 2);
}

/**
 * Define the osm2pgsql.ExpireOutput class/metatable.
 */
static void init_expire_output_class(lua_State *lua_state)
{
    lua_getglobal(lua_state, "osm2pgsql");
    if (luaL_newmetatable(lua_state, osm2pgsql_expire_output_name) != 1) {
        throw std::runtime_error{"Internal error: Lua newmetatable failed."};
    }
    lua_pushvalue(lua_state, -1); // Copy of new metatable

    // Add metatable as osm2pgsql.ExpireOutput so we can access it from Lua
    lua_setfield(lua_state, -3, "ExpireOutput");

    // Now add functions to metatable
    lua_pushvalue(lua_state, -1);
    lua_setfield(lua_state, -2, "__index");
    luaX_add_table_func(lua_state, "__tostring",
                        lua_trampoline_expire_output_tostring);
    luaX_add_table_func(lua_state, "name", lua_trampoline_expire_output_name);
    luaX_add_table_func(lua_state, "minzoom",
                        lua_trampoline_expire_output_minzoom);
    luaX_add_table_func(lua_state, "maxzoom",
                        lua_trampoline_expire_output_maxzoom);
    luaX_add_table_func(lua_state, "filename",
                        lua_trampoline_expire_output_filename);
    luaX_add_table_func(lua_state, "schema",
                        lua_trampoline_expire_output_schema);
    luaX_add_table_func(lua_state, "table", lua_trampoline_expire_output_table);

    lua_pop(lua_state, 2);
}

void output_flex_t::init_lua(std::string const &filename)
{
    m_lua_state.reset(luaL_newstate(),
                      [](lua_State *state) { lua_close(state); });

    setup_lua_environment(lua_state(), filename, get_options()->append);

    luaX_add_table_int(lua_state(), "stage", 1);

    luaX_add_table_func(lua_state(), "define_table",
                        lua_trampoline_app_define_table);

    luaX_add_table_func(lua_state(), "define_expire_output",
                        lua_trampoline_app_define_expire_output);

    init_table_class(lua_state());
    init_expire_output_class(lua_state());

    // Clean up stack
    lua_settop(lua_state(), 0);

    init_geometry_class(lua_state());

    assert(lua_gettop(lua_state()) == 0);

    // Load compiled in init.lua
    if (luaL_dostring(lua_state(), lua_init())) {
        throw fmt_error("Internal error in Lua setup: {}.",
                        lua_tostring(lua_state(), -1));
    }

    // Store the methods on OSM objects in its metatable.
    lua_getglobal(lua_state(), "object_metatable");
    lua_getfield(lua_state(), -1, "__index");
    luaX_add_table_func(lua_state(), "get_bbox", lua_trampoline_app_get_bbox);
    luaX_add_table_func(lua_state(), "as_linestring",
                        lua_trampoline_app_as_linestring);
    luaX_add_table_func(lua_state(), "as_point",
                        lua_trampoline_app_as_point);
    luaX_add_table_func(lua_state(), "as_polygon",
                        lua_trampoline_app_as_polygon);
    luaX_add_table_func(lua_state(), "as_multipoint",
                        lua_trampoline_app_as_multipoint);
    luaX_add_table_func(lua_state(), "as_multilinestring",
                        lua_trampoline_app_as_multilinestring);
    luaX_add_table_func(lua_state(), "as_multipolygon",
                        lua_trampoline_app_as_multipolygon);
    luaX_add_table_func(lua_state(), "as_geometrycollection",
                        lua_trampoline_app_as_geometrycollection);
    lua_settop(lua_state(), 0);

    // Store the global object "object_metatable" defined in the init.lua
    // script in the registry and then remove the global object. It will
    // later be used as metatable for OSM objects.
    lua_pushlightuserdata(lua_state(), (void *)osm2pgsql_object_metatable);
    lua_getglobal(lua_state(), "object_metatable");
    lua_settable(lua_state(), LUA_REGISTRYINDEX);
    lua_pushnil(lua_state());
    lua_setglobal(lua_state(), "object_metatable");

    assert(lua_gettop(lua_state()) == 0);

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
    m_select_relation_members = prepared_lua_function_t{
        lua_state(), calling_context::select_relation_members,
        "select_relation_members", 1};

    lua_remove(lua_state(), 1); // global "osm2pgsql"
}

idset_t const &output_flex_t::get_marked_way_ids()
{
    if (m_stage2_way_ids->empty()) {
        log_info("Skipping stage 1c (no marked ways).");
    } else {
        log_info("Entering stage 1c processing of {} ways...",
                 m_stage2_way_ids->size());
        m_stage2_way_ids->sort_unique();
    }

    return *m_stage2_way_ids;
}

void output_flex_t::reprocess_marked()
{
    if (m_stage2_way_ids->empty()) {
        log_info("No marked ways (Skipping stage 2).");
        return;
    }

    log_info("Reprocess marked ways (stage 2)...");

    if (!get_options()->append) {
        util::timer_t timer;

        for (auto &table : m_table_connections) {
            if (table.table().matches_type(osmium::item_type::way) &&
                table.table().has_id_column()) {
                table.analyze();
                table.create_id_index();
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

    m_stage2_way_ids->sort_unique();

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
