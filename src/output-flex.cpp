
#include "config.h"

#include "expire-tiles.hpp"
#include "flex-lua.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-flex.hpp"
#include "pgsql.hpp"
#include "reprojection.hpp"
#include "taginfo-impl.hpp"
#include "wkb.hpp"

#include <osmium/osm/types_from_string.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

// Lua can't call functions on C++ objects directly. This macro defines simple
// C "trampoline" functions which are called from Lua which get the current
// context (the output_flex_t object) and call the respective function on the
// context object.
#define TRAMPOLINE(func_name, lua_name)                                        \
    static int lua_trampoline_##func_name(lua_State *lua_state) noexcept       \
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
TRAMPOLINE(app_mark, mark)
TRAMPOLINE(app_get_bbox, get_bbox)
TRAMPOLINE(table_name, name)
TRAMPOLINE(table_schema, schema)
TRAMPOLINE(table_add_row, add_row)
TRAMPOLINE(table_columns, columns)
TRAMPOLINE(table_tostring, __tostring)

static char const osm2pgsql_table_name[] = "osm2pgsql.table";

static char const *type_to_char(osmium::item_type type) noexcept
{
    switch (type) {
    case osmium::item_type::node:
        return "N";
    case osmium::item_type::way:
        return "W";
    case osmium::item_type::relation:
        return "R";
    default:
        break;
    }
    return "X";
}

static void push_osm_object_to_lua_stack(lua_State *lua_state,
                                         osmium::OSMObject const &object,
                                         bool with_attributes)
{
    assert(lua_state);

    /**
     * Table will have 7 fields (id, version, timestamp, changeset, uid, user,
     * tags) for all object types plus 2 (is_closed, nodes) for ways or 1
     * (members) for relations.
     */
    constexpr int const max_table_size = 9;

    lua_createtable(lua_state, 0, max_table_size);

    luaX_add_table_int(lua_state, "id", object.id());

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
        luaX_add_table_bool(lua_state, "is_closed", way.is_closed());
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
                luaX_add_table_str(lua_state, "type", &tmp[0]);
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
}

static bool str2bool(char const *str) noexcept
{
    return (std::strcmp(str, "yes") == 0) || (std::strcmp(str, "true") == 0);
}

static int str2direction(char const *str) noexcept
{
    if ((std::strcmp(str, "yes") == 0) || (std::strcmp(str, "true") == 0) ||
        (std::strcmp(str, "1") == 0)) {
        return 1;
    }

    if (std::strcmp(str, "-1") == 0) {
        return -1;
    }

    return 0;
}

static int sgn(int val) noexcept
{
    if (val > 0) {
        return 1;
    }
    if (val < 0) {
        return -1;
    }
    return 0;
}

void output_flex_t::write_column(db_copy_mgr_t<db_deleter_by_id_t> *copy_mgr,
                                 flex_table_column_t const &column)
{
    lua_getfield(m_lua_state, -1, column.name().c_str());
    int const ltype = lua_type(m_lua_state, -1);

    // A Lua nil value is always translated to a database NULL
    if (ltype == LUA_TNIL) {
        copy_mgr->add_null_column();
        lua_pop(m_lua_state, 1);
        return;
    }

    if ((column.type() == table_column_type::sql) ||
        (column.type() == table_column_type::text)) {
        auto const *const str = lua_tolstring(m_lua_state, -1, nullptr);
        if (!str) {
            throw std::runtime_error{"Invalid type '{}' for text column"_format(
                lua_typename(m_lua_state, ltype))};
        }
        copy_mgr->add_column(str);
    } else if (column.type() == table_column_type::boolean) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(m_lua_state, -1) != 0);
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(lua_tointeger(m_lua_state, -1) != 0);
            break;
        case LUA_TSTRING:
            copy_mgr->add_column(
                str2bool(lua_tolstring(m_lua_state, -1, nullptr)));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for boolean column"_format(
                    lua_typename(m_lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::int2) {
        // cast here is on okay, because the database column is only 16bit
        copy_mgr->add_column((int16_t)lua_tointeger(m_lua_state, -1));
    } else if (column.type() == table_column_type::int4) {
        // cast here is on okay, because the database column is only 32bit
        copy_mgr->add_column((int32_t)lua_tointeger(m_lua_state, -1));
    } else if (column.type() == table_column_type::int8) {
        copy_mgr->add_column(lua_tointeger(m_lua_state, -1));
    } else if (column.type() == table_column_type::real) {
        copy_mgr->add_column(lua_tonumber(m_lua_state, -1));
    } else if (column.type() == table_column_type::hstore) {
        if (ltype == LUA_TTABLE) {
            copy_mgr->new_hash();

            lua_pushnil(m_lua_state);
            while (lua_next(m_lua_state, -2) != 0) {
                char const *const key = lua_tostring(m_lua_state, -2);
                char const *const val = lua_tostring(m_lua_state, -1);
                if (key == nullptr) {
                    int const ltype_key = lua_type(m_lua_state, -2);
                    throw std::runtime_error{
                        "NULL key for hstore. Possibly this is due to"
                        "an incorrect data type '{}' as key."_format(
                            lua_typename(m_lua_state, ltype_key))};
                }
                if (val == nullptr) {
                    int const ltype_value = lua_type(m_lua_state, -1);
                    throw std::runtime_error{
                        "NULL value for hstore. Possibly this is due to"
                        "an incorrect data type '{}' for key '{}'."_format(
                            lua_typename(m_lua_state, ltype_value), key)};
                }
                copy_mgr->add_hash_elem(key, val);
                lua_pop(m_lua_state, 1);
            }

            copy_mgr->finish_hash();
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for hstore column"_format(
                    lua_typename(m_lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::direction) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(m_lua_state, -1));
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(sgn(lua_tointeger(m_lua_state, -1)));
            break;
        case LUA_TSTRING:
            copy_mgr->add_column(
                str2direction(lua_tolstring(m_lua_state, -1, nullptr)));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for direction column"_format(
                    lua_typename(m_lua_state, ltype))};
        }
    } else {
        throw std::runtime_error{
            "Column type {} not implemented"_format(column.type())};
    }

    lua_pop(m_lua_state, 1);
}

void output_flex_t::write_row(flex_table_t *table, osmium::item_type id_type,
                              osmid_t id, std::string const &geom)
{
    assert(table);
    table->new_line();
    auto *copy_mgr = table->copy_mgr();

    for (auto const &column : *table) {
        if (column.type() == table_column_type::id_type) {
            copy_mgr->add_column(type_to_char(id_type));
        } else if (column.type() == table_column_type::id_num) {
            copy_mgr->add_column(id);
        } else if (column.is_geometry_column()) {
            copy_mgr->add_hex_geom(geom);
        } else if (column.type() == table_column_type::area) {
            if (geom.empty()) {
                copy_mgr->add_null_column();
            } else {
                double const area =
                    get_options()->reproject_area
                        ? ewkb::parser_t(geom).get_area<reprojection>(
                              get_options()->projection.get())
                        : ewkb::parser_t(geom)
                              .get_area<osmium::geom::IdentityProjection>();
                copy_mgr->add_column(area);
            }
        } else {
            write_column(copy_mgr, column);
        }
    }

    copy_mgr->finish_line();
}

int output_flex_t::app_mark()
{
    char const *type_name = luaL_checkstring(m_lua_state, 1);
    if (!type_name) {
        return 0;
    }

    osmium::object_id_type const id = luaL_checkinteger(m_lua_state, 2);

    if (type_name[0] == 'w') {
        m_ways_pending_tracker.mark(id);
    } else if (type_name[0] == 'r') {
        m_rels_pending_tracker.mark(id);
    }

    return 0;
}

// Gets all way nodes from the middle the first time this is called.
std::size_t output_flex_t::get_way_nodes()
{
    assert(m_context_way);
    if (m_num_way_nodes == std::numeric_limits<std::size_t>::max()) {
        m_num_way_nodes = m_mid->nodes_get_list(&m_context_way->nodes());
    }

    return m_num_way_nodes;
}

int output_flex_t::app_get_bbox()
{
    if (lua_gettop(m_lua_state) != 0) {
        throw std::runtime_error{"No parameter(s) needed for get_box()"};
    }

    if (m_context_node) {
        lua_pushnumber(m_lua_state, m_context_node->location().lon());
        lua_pushnumber(m_lua_state, m_context_node->location().lat());
        lua_pushnumber(m_lua_state, m_context_node->location().lon());
        lua_pushnumber(m_lua_state, m_context_node->location().lat());
        return 4;
    }

    if (m_context_way) {
        get_way_nodes();
        auto const bbox = m_context_way->envelope();
        if (bbox.valid()) {
            lua_pushnumber(m_lua_state, bbox.bottom_left().lon());
            lua_pushnumber(m_lua_state, bbox.bottom_left().lat());
            lua_pushnumber(m_lua_state, bbox.top_right().lon());
            lua_pushnumber(m_lua_state, bbox.top_right().lat());
            return 4;
        }
    }

    return 0;
}

static void check_name(std::string const &name, char const *in)
{
    auto const pos = name.find_first_of("\"',.;$%&/()<>{}=?^*#");

    if (pos == std::string::npos) {
        return;
    }

    throw std::runtime_error{
        "Special characters are not allowed in {} names: '{}'"_format(in,
                                                                      name)};
}

flex_table_t &output_flex_t::create_flex_table()
{
    std::string const table_name =
        luaX_get_table_string(m_lua_state, "name", -1, "The table");

    check_name(table_name, "table");

    auto const it = std::find_if(m_tables.cbegin(), m_tables.cend(),
                                 [&table_name](flex_table_t const &table) {
                                     return table.name() == table_name;
                                 });
    if (it != m_tables.cend()) {
        throw std::runtime_error{
            "Table with that name already exists: '{}'"_format(table_name)};
    }

    m_tables.emplace_back(table_name, get_options()->projection->target_srs(),
                          m_copy_thread, get_options()->append);
    auto &new_table = m_tables.back();

    lua_pop(m_lua_state, 1);

    // optional "schema" field
    lua_getfield(m_lua_state, -1, "schema");
    if (lua_isstring(m_lua_state, -1)) {
        std::string const schema = lua_tostring(m_lua_state, -1);
        check_name(schema, "schame");
        new_table.set_schema(schema);
    }

    lua_pop(m_lua_state, 1);

    return new_table;
}

void output_flex_t::setup_id_columns(flex_table_t *table)
{
    assert(table);
    lua_getfield(m_lua_state, -1, "ids");
    if (lua_type(m_lua_state, -1) != LUA_TTABLE) {
        fmt::print(stderr,
                   "WARNING! Table '{}' doesn't have an 'ids' column. Updates"
                   " and expire will not work!\n",
                   table->name());
        lua_pop(m_lua_state, 1); // ids
        return;
    }

    std::string const type{
        luaX_get_table_string(m_lua_state, "type", -1, "The ids field")};

    if (type == "node") {
        table->set_id_type(osmium::item_type::node);
    } else if (type == "way") {
        table->set_id_type(osmium::item_type::way);
    } else if (type == "relation") {
        table->set_id_type(osmium::item_type::relation);
    } else if (type == "area") {
        table->set_id_type(osmium::item_type::area);
    } else if (type == "any") {
        std::string type_column_name{"osm_type"};
        lua_getfield(m_lua_state, -1, "type_column");
        if (lua_isstring(m_lua_state, -1)) {
            type_column_name = lua_tolstring(m_lua_state, -1, nullptr);
            check_name(type_column_name, "column");
        }
        lua_pop(m_lua_state, 1); // type_column
        auto &column = table->add_column(type_column_name, "id_type");
        column.set_not_null_constraint();
        table->set_id_type(osmium::item_type::undefined);
    } else {
        throw std::runtime_error{"Unknown ids type: " + type};
    }

    std::string const name =
        luaX_get_table_string(m_lua_state, "id_column", -2, "The ids field");
    check_name(name, "column");

    auto &column = table->add_column(name, "id_num");
    column.set_not_null_constraint();
    lua_pop(m_lua_state, 3); // id_column, type, ids
}

void output_flex_t::setup_flex_table_columns(flex_table_t *table)
{
    assert(table);
    lua_getfield(m_lua_state, -1, "columns");
    if (lua_type(m_lua_state, -1) != LUA_TTABLE) {
        throw std::runtime_error{
            "No columns defined for table '{}'."_format(table->name())};
    }

    std::size_t num_columns = 0;
    lua_pushnil(m_lua_state);
    while (lua_next(m_lua_state, -2) != 0) {
        if (!lua_isnumber(m_lua_state, -2)) {
            throw std::runtime_error{
                "The 'columns' field must contain an array"};
        }
        if (!lua_istable(m_lua_state, -1)) {
            throw std::runtime_error{
                "The entries in the 'columns' array must be tables"};
        }

        char const *const type =
            luaX_get_table_string(m_lua_state, "type", -1, "Column entry");
        char const *const name =
            luaX_get_table_string(m_lua_state, "column", -2, "Column entry");
        check_name(name, "column");

        auto &new_column = table->add_column(name, type);

        if (new_column.is_linestring_column()) {
            lua_getfield(m_lua_state, -3, "split_at");
            if (lua_type(m_lua_state, -1) == LUA_TNUMBER) {
                new_column.set_split_at(lua_tonumber(m_lua_state, -1));
            } else if (lua_type(m_lua_state, -1) != LUA_TNIL) {
                throw std::runtime_error{
                    "Value of 'split_at' must be a number."};
            }
            lua_pop(m_lua_state, 1); // split_at
        }

        lua_pop(m_lua_state, 3); // column, type, table
        ++num_columns;
    }

    if (num_columns == 0) {
        throw std::runtime_error{
            "No columns defined for table '{}'."_format(table->name())};
    }
}

int output_flex_t::app_define_table()
{
    luaL_checktype(m_lua_state, 1, LUA_TTABLE);

    auto &new_table = create_flex_table();
    setup_id_columns(&new_table);
    setup_flex_table_columns(&new_table);

    lua_pushlightuserdata(m_lua_state, (void *)(m_tables.size()));
    luaL_getmetatable(m_lua_state, osm2pgsql_table_name);
    lua_setmetatable(m_lua_state, -2);

    return 1;
}

// Check function parameters of all osm2pgsql.table functions and return the
// flex table this function is on.
flex_table_t &output_flex_t::table_func_params(int n)
{
    if (lua_gettop(m_lua_state) != n) {
        throw std::runtime_error{"Need {} parameter(s)"_format(n)};
    }

    void *user_data = lua_touserdata(m_lua_state, 1);
    if (user_data == nullptr || !lua_getmetatable(m_lua_state, 1)) {
        throw std::runtime_error{
            "first parameter must be of type osm2pgsql.table"};
    }

    luaL_getmetatable(m_lua_state, osm2pgsql_table_name);
    if (!lua_rawequal(m_lua_state, -1, -2)) {
        throw std::runtime_error{
            "first parameter must be of type osm2pgsql.table"};
    }
    lua_pop(m_lua_state, 2);

    auto &table = m_tables.at(reinterpret_cast<uintptr_t>(user_data) - 1);
    lua_remove(m_lua_state, 1);
    return table;
}

int output_flex_t::table_tostring()
{
    auto const &table = table_func_params(1);

    std::string const str{"osm2pgsql.table[{}]"_format(table.name())};
    lua_pushstring(m_lua_state, str.c_str());

    return 1;
}

int output_flex_t::table_add_row()
{
    auto &table = table_func_params(2);
    luaL_checktype(m_lua_state, 1, LUA_TTABLE);

    if (m_context_node) {
        add_row(&table, *m_context_node);
    } else if (m_context_way) {
        add_row(&table, m_context_way);
    } else if (m_context_relation) {
        add_row(&table, *m_context_relation);
    }

    return 0;
}

int output_flex_t::table_columns()
{
    auto const &table = table_func_params(1);

    lua_createtable(m_lua_state, (int)table.num_columns(), 0);

    int n = 0;
    for (auto const &column : table) {
        lua_pushinteger(m_lua_state, ++n);
        lua_newtable(m_lua_state);

        luaX_add_table_str(m_lua_state, "name", column.name().c_str());
        luaX_add_table_str(m_lua_state, "type", column.type_name().c_str());
        luaX_add_table_str(m_lua_state, "sql_type",
                           column.sql_type_name(table.srid()).c_str());
        luaX_add_table_str(m_lua_state, "sql_modifiers",
                           column.sql_modifiers().c_str());

        if (column.is_linestring_column() && column.split_at() != 0.0) {
            luaX_add_table_num(m_lua_state, "split_at", column.split_at());
        }

        lua_rawset(m_lua_state, -3);
    }
    return 1;
}

int output_flex_t::table_name()
{
    auto const &table = table_func_params(1);
    lua_pushstring(m_lua_state, table.name().c_str());
    return 1;
}

int output_flex_t::table_schema()
{
    auto const &table = table_func_params(1);
    lua_pushstring(m_lua_state, table.schema().c_str());
    return 1;
}

void output_flex_t::add_row(flex_table_t *table, osmium::Node const &node)
{
    assert(table);
    std::string const wkb = m_builder.get_wkb_node(node.location());

    if (wkb.empty()) {
        return;
    }

    m_expire.from_wkb(wkb.c_str(), node.id());
    write_row(table, osmium::item_type::node, node.id(), wkb);
}

void output_flex_t::add_row(flex_table_t *table, osmium::Way *way)
{
    assert(table);
    assert(way);

    if (get_way_nodes() <= 1U) {
        return;
    }

    if (!table->has_geom_column()) {
        write_row(table, osmium::item_type::way, way->id(), "");
        return;
    }

    if (table->geom_column().is_polygon_column()) {
        if (way->is_closed()) {
            auto const wkb = m_builder.get_wkb_polygon(*way);
            if (!wkb.empty()) {
                m_expire.from_wkb(wkb.c_str(), way->id());
                write_row(table, osmium::item_type::way, way->id(), wkb);
            }
        }
        return;
    }

    double const split_at = table->geom_column().split_at();
    auto const wkbs = m_builder.get_wkb_line(way->nodes(), split_at);

    for (auto const &wkb : wkbs) {
        m_expire.from_wkb(wkb.c_str(), way->id());
        write_row(table, osmium::item_type::way, way->id(), wkb);
    }
}

void output_flex_t::add_row(flex_table_t *table,
                            osmium::Relation const &relation)
{
    assert(table);

    osmid_t const id =
        table->map_id(osmium::item_type::relation, relation.id());

    if (!table->has_geom_column()) {
        write_row(table, osmium::item_type::relation, id, "");
        return;
    }

    m_buffer.clear();
    auto const num_ways =
        m_mid->rel_way_members_get(relation, nullptr, m_buffer);

    if (num_ways == 0) {
        return;
    }

    for (auto &way : m_buffer.select<osmium::Way>()) {
        m_mid->nodes_get_list(&(way.nodes()));
    }

    if (table->geom_column().is_polygon_column()) {
        bool const want_mp =
            table->geom_column().type() == table_column_type::multipolygon;
        auto const wkbs =
            m_builder.get_wkb_multipolygon(relation, m_buffer, want_mp);

        for (auto const &wkb : wkbs) {
            m_expire.from_wkb(wkb.c_str(), id);
            write_row(table, osmium::item_type::relation, id, wkb);
        }
        return;
    }

    if (table->geom_column().is_linestring_column()) {
        double const split_at = table->geom_column().split_at();
        auto const wkbs = m_builder.get_wkb_multiline(m_buffer, split_at);

        for (auto const &wkb : wkbs) {
            m_expire.from_wkb(wkb.c_str(), id);
            write_row(table, osmium::item_type::relation, id, wkb);
        }
    }
}

void output_flex_t::call_process_function(int index,
                                          osmium::OSMObject const &object)
{
    assert(lua_gettop(m_lua_state) == 3);

    lua_pushvalue(m_lua_state, index); // the function to call
    push_osm_object_to_lua_stack(
        m_lua_state, object,
        get_options()->extra_attributes); // the single argument

    if (lua_pcall(m_lua_state, 1, 0, 0)) {
        throw std::runtime_error{"Failed to execute lua processing function:"
                                 " {}"_format(lua_tostring(m_lua_state, -1))};
    }
}

void output_flex_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id,
                                 std::size_t output_id, std::size_t &added)
{
    fmt::print(stderr, "enqueue_ways: {}/{}\n", id, output_id);
    osmid_t const prev = m_ways_pending_tracker.last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.push(pending_job_t(id, output_id));
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if (!m_ways_done_tracker->is_marked(id) && id_tracker::is_valid(id)) {
        job_queue.push(pending_job_t(id, output_id));
        added++;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = m_ways_pending_tracker.pop_mark();
    if (!id_tracker::is_valid(popped)) {
        return;
    }

    //get all the ones up to the id that was passed in
    while (popped < id) {
        if (!m_ways_done_tracker->is_marked(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
        popped = m_ways_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if (popped > id) {
        if (!m_ways_done_tracker->is_marked(popped) &&
            id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

void output_flex_t::pending_way(osmid_t id, int exists)
{
    if (!m_has_process_way) {
        return;
    }

    m_buffer.clear();
    if (!m_mid->ways_get(id, m_buffer)) {
        return;
    }

    if (exists) {
        way_delete(id);
        auto const rel_ids = m_mid->relations_using_way(id);
        for (auto const id : rel_ids) {
            m_rels_pending_tracker.mark(id);
        }
    }

    auto &way = m_buffer.get<osmium::Way>(0);

    m_context_way = &way;
    call_process_function(2, way);
    m_context_way = nullptr;
    m_num_way_nodes = std::numeric_limits<std::size_t>::max();
    m_buffer.clear();
}

void output_flex_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id,
                                      std::size_t output_id, std::size_t &added)
{
    if (!m_has_process_relation) {
        return;
    }

    osmid_t const prev = m_rels_pending_tracker.last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.emplace(id, output_id);
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if (id_tracker::is_valid(id)) {
        job_queue.emplace(id, output_id);
        ++added;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = m_rels_pending_tracker.pop_mark();
    if (!id_tracker::is_valid(popped)) {
        return;
    }

    //get all the ones up to the id that was passed in
    while (popped < id) {
        job_queue.emplace(popped, output_id);
        ++added;
        popped = m_rels_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if (popped > id) {
        if (id_tracker::is_valid(popped)) {
            job_queue.emplace(popped, output_id);
            ++added;
        }
    }
}

void output_flex_t::pending_relation(osmid_t id, int exists)
{
    if (!m_has_process_relation) {
        return;
    }

    // Try to fetch the relation from the DB
    // Note that we cannot use the global buffer here because
    // we cannot keep a reference to the relation and an autogrow buffer
    // might be relocated when more data is added.
    if (!m_mid->relations_get(id, m_rels_buffer)) {
        return;
    }

    // If the flag says this object may exist already, delete it first.
    if (exists) {
        delete_from_tables(osmium::item_type::relation, id);
    }

    auto const &relation = m_rels_buffer.get<osmium::Relation>(0);

    m_context_relation = &relation;
    call_process_function(3, relation);
    m_context_relation = nullptr;
    m_rels_buffer.clear();
}

void output_flex_t::commit()
{
    for (auto &table : m_tables) {
        table.commit();
    }

    // Run Lua garbage collection
    lua_gc(m_lua_state, LUA_GCCOLLECT, 0);
}

std::shared_ptr<std::string> output_flex_t::read_userdata() const
{
    // If a previous run of this function already got the userdata, return it.
    if (m_userdata) {
        return m_userdata;
    }

    lua_gc(m_lua_state, LUA_GCCOLLECT, 0);
    int const lua_memory_with_userdata = lua_gc(m_lua_state, LUA_GCCOUNT, 0);

    // Clean Lua stack
    lua_settop(m_lua_state, 0);

    // If there is no userdata, return an empty string.
    lua_getglobal(m_lua_state, "osm2pgsql");
    lua_getfield(m_lua_state, -1, "userdata");
    if (lua_type(m_lua_state, -1) == LUA_TNIL) {
        lua_pop(m_lua_state, 2); // userdata, osm2pgsql
        m_userdata.reset(new std::string{});
        fmt::print(stderr, "No Lua user data used\n");
        return m_userdata;
    }

    lua_settop(m_lua_state, 1); // leave only osm2pgsql on stack

    // call ... = osm2pgsql.messagepack.pack(osm2pgsql.userdata)
    lua_getfield(m_lua_state, -1, "messagepack");
    lua_getfield(m_lua_state, -1, "pack");
    lua_getfield(m_lua_state, -3, "userdata");
    if (lua_pcall(m_lua_state, 1, 1, 0)) {
        throw std::runtime_error{"Failed to execute lua pack function:"
                                 " {}"_format(lua_tostring(m_lua_state, -1))};
    }

    std::size_t len = 0;
    char const *str = lua_tolstring(m_lua_state, -1, &len);
    m_userdata.reset(new std::string(str, len));

    // Now that we have read the user data we can clean it up and recover the
    // memory.
    lua_settop(m_lua_state, 1); // leave only osm2pgsql on stack
    lua_pushnil(m_lua_state);
    lua_setfield(m_lua_state, -2, "userdata");

    lua_settop(m_lua_state, 0);
    lua_gc(m_lua_state, LUA_GCCOLLECT, 0);
    int const lua_memory_without_userdata = lua_gc(m_lua_state, LUA_GCCOUNT, 0);
    fmt::print(stderr, "Lua used about {} MBytes for user data\n",
               (lua_memory_with_userdata - lua_memory_without_userdata) / 1024);
    fmt::print(stderr, "The user data takes {} MBytes when serialized\n",
               len / (1024 * 1024));

    return m_userdata;
}

void output_flex_t::stop(osmium::thread::Pool *pool)
{
    for (auto &table : m_tables) {
        pool->submit([&]() {
            table.stop(m_options.slim & !m_options.droptemp,
                       m_options.tblsmain_data.get_value_or(""));
        });
    }

    if (m_options.expire_tiles_zoom_min > 0) {
        m_expire.output_and_destroy(m_options.expire_tiles_filename.c_str(),
                                    m_options.expire_tiles_zoom_min);
    }
}

void output_flex_t::node_add(osmium::Node const &node)
{
    if (!m_has_process_node) {
        return;
    }

    m_context_node = &node;
    call_process_function(1, node);
    m_context_node = nullptr;
}

void output_flex_t::way_add(osmium::Way *way)
{
    assert(way);

    if (!m_has_process_way) {
        return;
    }

    m_context_way = way;
    call_process_function(2, *way);
    m_context_way = nullptr;
    m_num_way_nodes = std::numeric_limits<std::size_t>::max();
}

void output_flex_t::relation_add(osmium::Relation const &relation)
{
    if (!m_has_process_relation) {
        return;
    }

    m_context_relation = &relation;
    call_process_function(3, relation);
    m_context_relation = nullptr;
}

void output_flex_t::delete_from_table(flex_table_t *table,
                                      osmium::item_type type, osmid_t osm_id)
{
    assert(table);
    auto const id = table->map_id(type, osm_id);
    auto const result = table->get_geom_by_id(id);
    if (m_expire.from_result(result, id) != 0) {
        table->delete_rows_with(id);
    }
}

void output_flex_t::delete_from_tables(osmium::item_type type, osmid_t osm_id)
{
    for (auto &table : m_tables) {
        if (table.matches_type(type)) {
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

void output_flex_t::init_clone()
{
    for (auto &table : m_tables) {
        table.connect(m_options.database_options.conninfo());
        table.prepare();
    }
}

void output_flex_t::start()
{
    for (auto &table : m_tables) {
        table.start(m_options.database_options.conninfo(),
                    m_options.tblsmain_data.get_value_or(""));
    }
}

std::shared_ptr<output_t>
output_flex_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &copy_thread) const
{
    return std::shared_ptr<output_t>(new output_flex_t{
        mid, *get_options(), copy_thread, true, read_userdata()});
}

output_flex_t::output_flex_t(
    std::shared_ptr<middle_query_t> const &mid, options_t const &o,
    std::shared_ptr<db_copy_thread_t> const &copy_thread, bool is_clone,
    std::shared_ptr<std::string> userdata)
: output_t(mid, o), m_builder(o.projection),
  m_expire(o.expire_tiles_zoom, o.expire_tiles_max_bbox, o.projection),
  m_ways_done_tracker(new id_tracker{}),
  m_buffer(32768, osmium::memory::Buffer::auto_grow::yes),
  m_rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_copy_thread(copy_thread), m_stage(is_clone ? 2U : 1U)
{
    init_lua(m_options.style, std::move(userdata));

    for (auto &table : m_tables) {
        table.init();
    }

    if (is_clone) {
        init_clone();
    }
}

static bool prepare_process_function(lua_State *lua_state, char const *name)
{
    lua_getfield(lua_state, 1, name);

    if (lua_type(lua_state, -1) == LUA_TFUNCTION) {
        return true;
    }

    if (lua_type(lua_state, -1) == LUA_TNIL) {
        return false;
    }

    throw std::runtime_error{"osm2pgsql.{} must be a function"_format(name)};
}

void output_flex_t::init_lua(std::string const &filename,
                             std::shared_ptr<std::string> userdata)
{
    m_lua_state = luaL_newstate();

    // Set up global lua libs
    luaL_openlibs(m_lua_state);

    // Set up global "osm2pgsql" object
    lua_newtable(m_lua_state);

    luaX_add_table_str(m_lua_state, "version", VERSION);
    luaX_add_table_int(m_lua_state, "srid",
                       get_options()->projection->target_srs());
    luaX_add_table_str(m_lua_state, "mode",
                       m_options.append ? "append" : "create");
    luaX_add_table_int(m_lua_state, "stage", m_stage);

    // Add empty "userdata" table
    lua_pushliteral(m_lua_state, "userdata");
    lua_newtable(m_lua_state);
    lua_rawset(m_lua_state, -3);

    luaX_add_table_func(m_lua_state, "define_table",
                        lua_trampoline_app_define_table);
    luaX_add_table_func(m_lua_state, "mark", lua_trampoline_app_mark);
    luaX_add_table_func(m_lua_state, "get_bbox", lua_trampoline_app_get_bbox);

    lua_setglobal(m_lua_state, "osm2pgsql");

    if (luaL_dostring(m_lua_state, R"(

function osm2pgsql._define_table_impl(_type, _name, _columns)
    return osm2pgsql.define_table{
        name = _name,
        ids = { type = _type, id_column = _type .. '_id' },
        columns = _columns,
    }
end

function osm2pgsql.define_node_table(_name, _columns)
    return osm2pgsql._define_table_impl('node', _name, _columns)
end

function osm2pgsql.define_way_table(_name, _columns)
    return osm2pgsql._define_table_impl('way', _name, _columns)
end

function osm2pgsql.define_relation_table(_name, _columns)
    return osm2pgsql._define_table_impl('relation', _name, _columns)
end

function osm2pgsql.define_area_table(_name, _columns)
    return osm2pgsql._define_table_impl('area', _name, _columns)
end

                      )")) {
        throw std::runtime_error{"Internal error: Lua setup"};
    }

    if (luaL_dostring(m_lua_state,
                      "osm2pgsql.messagepack = require('MessagePack')")) {
        throw std::runtime_error{"loading MessagePack failed"};
    }

    assert(lua_gettop(m_lua_state) == 0);

    if (userdata && !userdata->empty()) {
        // call osm2pgsql.userdata = osm2pgsql.messagepack.unpack(...)
        lua_getglobal(m_lua_state, "osm2pgsql");
        lua_getfield(m_lua_state, -1, "messagepack");
        lua_getfield(m_lua_state, -1, "unpack");
        lua_pushlstring(m_lua_state, userdata->data(), userdata->size());
        if (lua_pcall(m_lua_state, 1, 1, 0)) {
            throw std::runtime_error{
                "Failed to execute lua pack function:"
                " {}"_format(lua_tostring(m_lua_state, -1))};
        }
        lua_setfield(m_lua_state, 1, "userdata");
        lua_settop(m_lua_state, 0);
        userdata.reset();
    }

    assert(lua_gettop(m_lua_state) == 0);

    luaX_set_context(m_lua_state, this);

    // Define "osmpgsql.table" metatable
    if (luaL_newmetatable(m_lua_state, osm2pgsql_table_name) != 1) {
        throw std::runtime_error{"newmetatable failed"};
    }
    lua_pushvalue(m_lua_state, -1);
    lua_setfield(m_lua_state, -2, "__index");
    luaX_add_table_func(m_lua_state, "__tostring",
                        lua_trampoline_table_tostring);
    luaX_add_table_func(m_lua_state, "add_row", lua_trampoline_table_add_row);
    luaX_add_table_func(m_lua_state, "name", lua_trampoline_table_name);
    luaX_add_table_func(m_lua_state, "schema", lua_trampoline_table_schema);
    luaX_add_table_func(m_lua_state, "columns", lua_trampoline_table_columns);
    lua_pop(m_lua_state, 1);

    // Load user config file
    if (luaL_dofile(m_lua_state, filename.c_str())) {
        throw std::runtime_error{"Error loading lua config: {}"_format(
            lua_tostring(m_lua_state, -1))};
    }

    // Check that the process_* functions are available and store them in the
    // Lua stack for fast access later.
    lua_getglobal(m_lua_state, "osm2pgsql");
    m_has_process_node = prepare_process_function(m_lua_state, "process_node");
    m_has_process_way = prepare_process_function(m_lua_state, "process_way");
    m_has_process_relation =
        prepare_process_function(m_lua_state, "process_relation");

    lua_remove(m_lua_state, 1);
}

output_flex_t::~output_flex_t() noexcept { lua_close(m_lua_state); }

std::size_t output_flex_t::pending_count() const
{
    return m_ways_pending_tracker.size() + m_rels_pending_tracker.size();
}

void output_flex_t::merge_pending_relations(output_t *other)
{
    auto *opgsql = dynamic_cast<output_flex_t *>(other);
    if (opgsql) {
        osmid_t id;
        while (id_tracker::is_valid(
            (id = opgsql->m_rels_pending_tracker.pop_mark()))) {
            m_rels_pending_tracker.mark(id);
        }
    }
}

void output_flex_t::merge_expire_trees(output_t *other)
{
    auto *opgsql = dynamic_cast<output_flex_t *>(other);
    if (opgsql) {
        m_expire.merge_and_destroy(opgsql->m_expire);
    }
}
