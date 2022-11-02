/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "flex-lua-geom.hpp"
#include "format.hpp"
#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-transform.hpp"
#include "logging.hpp"
#include "lua-init.hpp"
#include "lua-utils.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-flex.hpp"
#include "pgsql.hpp"
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

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <boost/filesystem.hpp>

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

static char const *const osm2pgsql_table_name = "osm2pgsql.Table";
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

    throw std::runtime_error{"osm2pgsql.{} must be a function."_format(name)};
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

static int sgn(double val) noexcept
{
    if (val > 0) {
        return 1;
    }
    if (val < 0) {
        return -1;
    }
    return 0;
}

class not_null_exception : public std::runtime_error
{
public:
    not_null_exception(std::string const &message,
                       flex_table_column_t const *column)
    : std::runtime_error(message), m_column(column)
    {}

    flex_table_column_t const &column() const noexcept { return *m_column; }

private:
    flex_table_column_t const *m_column;
}; // class not_null_exception

static void write_null(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                       flex_table_column_t const &column)
{
    if (column.not_null()) {
        throw not_null_exception{
            "Can not add NULL to column '{}' declared NOT NULL."_format(
                column.name()),
            &column};
    }
    copy_mgr->add_null_column();
}

static void write_boolean(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                          flex_table_column_t const &column, char const *str)
{
    if ((std::strcmp(str, "yes") == 0) || (std::strcmp(str, "true") == 0) ||
        std::strcmp(str, "1") == 0) {
        copy_mgr->add_column(true);
        return;
    }

    if ((std::strcmp(str, "no") == 0) || (std::strcmp(str, "false") == 0) ||
        std::strcmp(str, "0") == 0) {
        copy_mgr->add_column(false);
        return;
    }

    write_null(copy_mgr, column);
}

static void
write_direction(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                flex_table_column_t const &column, char const *str)
{
    if ((std::strcmp(str, "yes") == 0) || (std::strcmp(str, "1") == 0)) {
        copy_mgr->add_column(1);
        return;
    }

    if ((std::strcmp(str, "no") == 0) || (std::strcmp(str, "0") == 0)) {
        copy_mgr->add_column(0);
        return;
    }

    if (std::strcmp(str, "-1") == 0) {
        copy_mgr->add_column(-1);
        return;
    }

    write_null(copy_mgr, column);
}

template <typename T>
void write_integer(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                   flex_table_column_t const &column, char const *str)
{
    if (*str == '\0') {
        write_null(copy_mgr, column);
        return;
    }

    char *end = nullptr;
    errno = 0;
    auto const value = std::strtoll(str, &end, 10);

    if (errno != 0 || *end != '\0') {
        write_null(copy_mgr, column);
        return;
    }

    if (value >= std::numeric_limits<T>::min() &&
        value <= std::numeric_limits<T>::max()) {
        copy_mgr->add_column(value);
        return;
    }

    write_null(copy_mgr, column);
}

static void write_double(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                         flex_table_column_t const &column, char const *str)
{
    if (*str == '\0') {
        write_null(copy_mgr, column);
        return;
    }

    char *end = nullptr;
    double const value = std::strtod(str, &end);

    if (end && *end != '\0') {
        write_null(copy_mgr, column);
        return;
    }

    copy_mgr->add_column(value);
}

using json_writer_type = rapidjson::Writer<rapidjson::StringBuffer>;
using table_register_type = std::vector<void const *>;

/**
 * Check that the value on the top of the Lua stack is a simple array.
 * This means that all keys must be consecutive integers starting from 1.
 */
static bool is_lua_array(lua_State *lua_state)
{
    uint32_t n = 1;
    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        lua_pop(lua_state, 1); // remove value from stack
#if LUA_VERSION_NUM >= 503
        if (!lua_isinteger(lua_state, -1)) {
            lua_pop(lua_state, 1);
            return false;
        }
        int okay = 0;
        auto const num = lua_tointegerx(lua_state, -1, &okay);
        if (!okay || num != n++) {
            lua_pop(lua_state, 1);
            return false;
        }
#else
        if (!lua_isnumber(lua_state, -1)) {
            lua_pop(lua_state, 1);
            return false;
        }
        double const num = lua_tonumber(lua_state, -1);
        double intpart = 0.0;
        if (std::modf(num, &intpart) != 0.0 || intpart < 0 ||
            static_cast<uint32_t>(num) != n++) {
            lua_pop(lua_state, 1);
            return false;
        }
#endif
    }

    // An empty lua table could be both, we decide here that it is not stored
    // as a JSON array but as a JSON object.
    return n != 1;
}

static void write_json(json_writer_type *writer, lua_State *lua_state,
                       table_register_type *tables);

static void write_json_table(json_writer_type *writer, lua_State *lua_state,
                             table_register_type *tables)
{
    void const *table_ptr = lua_topointer(lua_state, -1);
    assert(table_ptr);
    auto const it = std::find(tables->cbegin(), tables->cend(), table_ptr);
    if (it != tables->cend()) {
        throw std::runtime_error{"Loop detected in table"};
    }
    tables->push_back(table_ptr);

    if (is_lua_array(lua_state)) {
        writer->StartArray();
        lua_pushnil(lua_state);
        while (lua_next(lua_state, -2) != 0) {
            write_json(writer, lua_state, tables);
            lua_pop(lua_state, 1);
        }
        writer->EndArray();
    } else {
        writer->StartObject();
        lua_pushnil(lua_state);
        while (lua_next(lua_state, -2) != 0) {
            int const ltype_key = lua_type(lua_state, -2);
            if (ltype_key != LUA_TSTRING) {
                throw std::runtime_error{
                    "Incorrect data type '{}' as key."_format(
                        lua_typename(lua_state, ltype_key))};
            }
            char const *const key = lua_tostring(lua_state, -2);
            writer->Key(key);
            write_json(writer, lua_state, tables);
            lua_pop(lua_state, 1);
        }
        writer->EndObject();
    }
}

static void write_json_number(json_writer_type *writer, lua_State *lua_state)
{
#if LUA_VERSION_NUM >= 503
    int okay = 0;
    auto const num = lua_tointegerx(lua_state, -1, &okay);
    if (okay) {
        writer->Int64(num);
    } else {
        writer->Double(lua_tonumber(lua_state, -1));
    }
#else
    double const num = lua_tonumber(lua_state, -1);
    double intpart = 0.0;
    if (std::modf(num, &intpart) == 0.0) {
        writer->Int64(static_cast<int64_t>(num));
    } else {
        writer->Double(num);
    }
#endif
}

static void write_json(json_writer_type *writer, lua_State *lua_state,
                       table_register_type *tables)
{
    assert(writer);
    assert(lua_state);

    int const ltype = lua_type(lua_state, -1);
    switch (ltype) {
    case LUA_TNIL:
        writer->Null();
        break;
    case LUA_TBOOLEAN:
        writer->Bool(lua_toboolean(lua_state, -1) != 0);
        break;
    case LUA_TNUMBER:
        write_json_number(writer, lua_state);
        break;
    case LUA_TSTRING:
        writer->String(lua_tostring(lua_state, -1));
        break;
    case LUA_TTABLE:
        write_json_table(writer, lua_state, tables);
        break;
    default:
        throw std::runtime_error{
            "Invalid type '{}' for json/jsonb column."_format(
                lua_typename(lua_state, ltype))};
    }
}

static bool is_compatible(geom::geometry_t const &geom,
                          table_column_type type) noexcept
{
    switch (type) {
    case table_column_type::geometry:
        return true;
    case table_column_type::point:
        return geom.is_point();
    case table_column_type::linestring:
        return geom.is_linestring();
    case table_column_type::polygon:
        return geom.is_polygon();
    case table_column_type::multipoint:
        return geom.is_point() || geom.is_multipoint();
    case table_column_type::multilinestring:
        return geom.is_linestring() || geom.is_multilinestring();
    case table_column_type::multipolygon:
        return geom.is_polygon() || geom.is_multipolygon();
    case table_column_type::geometrycollection:
        return geom.is_collection();
    default:
        break;
    }
    return false;
}

void output_flex_t::write_column(
    db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
    flex_table_column_t const &column)
{
    // If there is nothing on the Lua stack, then the Lua function add_row()
    // was called without a table parameter. In that case this column will
    // be set to NULL.
    if (lua_gettop(lua_state()) == 0) {
        write_null(copy_mgr, column);
        return;
    }

    lua_getfield(lua_state(), -1, column.name().c_str());
    int const ltype = lua_type(lua_state(), -1);

    // Certain Lua types can never be added to the database
    if (ltype == LUA_TFUNCTION || ltype == LUA_TTHREAD) {
        throw std::runtime_error{
            "Can not add Lua objects of type function or thread."};
    }

    // A Lua nil value is always translated to a database NULL
    if (ltype == LUA_TNIL) {
        write_null(copy_mgr, column);
        lua_pop(lua_state(), 1);
        return;
    }

    if (column.type() == table_column_type::text) {
        auto const *const str = lua_tolstring(lua_state(), -1, nullptr);
        if (!str) {
            throw std::runtime_error{
                "Invalid type '{}' for text column."_format(
                    lua_typename(lua_state(), ltype))};
        }
        copy_mgr->add_column(str);
    } else if (column.type() == table_column_type::boolean) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(lua_state(), -1) != 0);
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(lua_tonumber(lua_state(), -1) != 0);
            break;
        case LUA_TSTRING:
            write_boolean(copy_mgr, column,
                          lua_tolstring(lua_state(), -1, nullptr));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for boolean column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.type() == table_column_type::int2) {
        if (ltype == LUA_TNUMBER) {
            int64_t const value = lua_tointeger(lua_state(), -1);
            if (value >= std::numeric_limits<int16_t>::min() &&
                value <= std::numeric_limits<int16_t>::max()) {
                copy_mgr->add_column(value);
            } else {
                write_null(copy_mgr, column);
            }
        } else if (ltype == LUA_TSTRING) {
            write_integer<int16_t>(copy_mgr, column,
                                   lua_tolstring(lua_state(), -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state(), -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int2 column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.type() == table_column_type::int4) {
        if (ltype == LUA_TNUMBER) {
            int64_t const value = lua_tointeger(lua_state(), -1);
            if (value >= std::numeric_limits<int32_t>::min() &&
                value <= std::numeric_limits<int32_t>::max()) {
                copy_mgr->add_column(value);
            } else {
                write_null(copy_mgr, column);
            }
        } else if (ltype == LUA_TSTRING) {
            write_integer<int32_t>(copy_mgr, column,
                                   lua_tolstring(lua_state(), -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state(), -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int4 column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.type() == table_column_type::int8) {
        if (ltype == LUA_TNUMBER) {
            copy_mgr->add_column(lua_tointeger(lua_state(), -1));
        } else if (ltype == LUA_TSTRING) {
            write_integer<int64_t>(copy_mgr, column,
                                   lua_tolstring(lua_state(), -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state(), -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int8 column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.type() == table_column_type::real) {
        if (ltype == LUA_TNUMBER) {
            copy_mgr->add_column(lua_tonumber(lua_state(), -1));
        } else if (ltype == LUA_TSTRING) {
            write_double(copy_mgr, column,
                         lua_tolstring(lua_state(), -1, nullptr));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for real column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.type() == table_column_type::hstore) {
        if (ltype == LUA_TTABLE) {
            copy_mgr->new_hash();

            lua_pushnil(lua_state());
            while (lua_next(lua_state(), -2) != 0) {
                char const *const key = lua_tostring(lua_state(), -2);
                char const *const val = lua_tostring(lua_state(), -1);
                if (key == nullptr) {
                    int const ltype_key = lua_type(lua_state(), -2);
                    throw std::runtime_error{
                        "NULL key for hstore. Possibly this is due to"
                        " an incorrect data type '{}' as key."_format(
                            lua_typename(lua_state(), ltype_key))};
                }
                if (val == nullptr) {
                    int const ltype_value = lua_type(lua_state(), -1);
                    throw std::runtime_error{
                        "NULL value for hstore. Possibly this is due to"
                        " an incorrect data type '{}' for key '{}'."_format(
                            lua_typename(lua_state(), ltype_value), key)};
                }
                copy_mgr->add_hash_elem(key, val);
                lua_pop(lua_state(), 1);
            }

            copy_mgr->finish_hash();
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for hstore column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if ((column.type() == table_column_type::json) ||
               (column.type() == table_column_type::jsonb)) {
        rapidjson::StringBuffer stream;
        json_writer_type writer{stream};
        table_register_type tables;
        write_json(&writer, lua_state(), &tables);
        copy_mgr->add_column(stream.GetString());
    } else if (column.type() == table_column_type::direction) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(lua_state(), -1));
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(sgn(lua_tonumber(lua_state(), -1)));
            break;
        case LUA_TSTRING:
            write_direction(copy_mgr, column,
                            lua_tolstring(lua_state(), -1, nullptr));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for direction column."_format(
                    lua_typename(lua_state(), ltype))};
        }
    } else if (column.is_geometry_column()) {
        // If this is a geometry column, the Lua function 'insert()' was
        // called, because for 'add_row()' geometry columns are handled
        // earlier and 'write_column()' is not called.
        if (ltype == LUA_TUSERDATA) {
            auto const *const geom = unpack_geometry(lua_state(), -1);
            if (geom && !geom->is_null()) {
                auto const type = column.type();
                if (!is_compatible(*geom, type)) {
                    throw std::runtime_error{
                        "Geometry data for geometry column '{}'"
                        " has the wrong type ({})."_format(
                            column.name(), geometry_type(*geom))};
                }
                bool const wrap_multi =
                    (type == table_column_type::multipoint ||
                     type == table_column_type::multilinestring ||
                     type == table_column_type::multipolygon);
                if (geom->srid() == column.srid()) {
                    m_expire.from_geometry(*geom);
                    copy_mgr->add_hex_geom(geom_to_ewkb(*geom, wrap_multi));
                } else {
                    auto const proj =
                        reprojection::create_projection(column.srid());
                    auto const tgeom = geom::transform(*geom, *proj);
                    m_expire.from_geometry(tgeom);
                    copy_mgr->add_hex_geom(geom_to_ewkb(tgeom, wrap_multi));
                }
            } else {
                write_null(copy_mgr, column);
            }
        } else {
            throw std::runtime_error{
                "Need geometry data for geometry column '{}'."_format(
                    column.name())};
        }
    } else if (column.type() == table_column_type::area) {
        // If this is an area column, the Lua function 'insert()' was
        // called, because for 'add_row()' area columns are handled
        // earlier and 'write_column()' is not called.
        throw std::runtime_error{"Column type 'area' not allowed with "
                                 "'insert()'. Maybe use 'real'?"};
    } else {
        throw std::runtime_error{"Column type {} not implemented."_format(
            static_cast<uint8_t>(column.type()))};
    }

    lua_pop(lua_state(), 1);
}

void output_flex_t::write_row(table_connection_t *table_connection,
                              osmium::item_type id_type, osmid_t id,
                              geom::geometry_t const &geom, int srid)
{
    assert(table_connection);
    table_connection->new_line();
    auto *copy_mgr = table_connection->copy_mgr();

    geom::geometry_t projected_geom;
    geom::geometry_t const* output_geom = &geom;
    if (srid && geom.srid() != srid) {
        auto const proj = reprojection::create_projection(srid);
        projected_geom = geom::transform(geom, *proj);
        output_geom = &projected_geom;
    }

    for (auto const &column : table_connection->table()) {
        if (column.create_only()) {
            continue;
        }
        if (column.type() == table_column_type::id_type) {
            copy_mgr->add_column(type_to_char(id_type));
        } else if (column.type() == table_column_type::id_num) {
            copy_mgr->add_column(id);
        } else if (column.is_geometry_column()) {
            assert(!geom.is_null());
            auto const type = column.type();
            bool const wrap_multi =
                (type == table_column_type::multilinestring ||
                 type == table_column_type::multipolygon);
            copy_mgr->add_hex_geom(geom_to_ewkb(*output_geom, wrap_multi));
        } else if (column.type() == table_column_type::area) {
            if (geom.is_null()) {
                write_null(copy_mgr, column);
            } else {
                // if srid of the area column is the same as for the geom column
                double area = 0;
                if (column.srid() == 4326) {
                    area = geom::area(geom);
                } else if (column.srid() == srid) {
                    area = geom::area(projected_geom);
                } else {
                    // XXX there is some overhead here always dynamically
                    // creating the same projection object. Needs refactoring.
                    auto const mproj = reprojection::create_projection(column.srid());
                    area = geom::area(geom::transform(geom, *mproj));
                }
                copy_mgr->add_column(area);
            }
        } else {
            write_column(copy_mgr, column);
        }
    }

    copy_mgr->finish_line();
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
        throw std::runtime_error{
            "The function {}() can only be called from the {}."_format(
                name, context)};
    }

    if (lua_gettop(lua_state()) > 1) {
        throw std::runtime_error{
            "No parameter(s) needed for {}()."_format(name)};
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

flex_table_t &output_flex_t::create_flex_table()
{
    std::string const table_name =
        luaX_get_table_string(lua_state(), "name", -1, "The table");

    check_identifier(table_name, "table names");

    auto const it = std::find_if(m_tables->cbegin(), m_tables->cend(),
                                 [&table_name](flex_table_t const &table) {
                                     return table.name() == table_name;
                                 });
    if (it != m_tables->cend()) {
        throw std::runtime_error{
            "Table with name '{}' already exists."_format(table_name)};
    }

    m_tables->emplace_back(table_name);
    auto &new_table = m_tables->back();

    lua_pop(lua_state(), 1);

    // optional "schema" field
    lua_getfield(lua_state(), -1, "schema");
    if (lua_isstring(lua_state(), -1)) {
        std::string const schema = lua_tostring(lua_state(), -1);
        check_identifier(schema, "schema field");
        new_table.set_schema(schema);
    }
    lua_pop(lua_state(), 1);

    // optional "cluster" field
    lua_getfield(lua_state(), -1, "cluster");
    int const cluster_type = lua_type(lua_state(), -1);
    if (cluster_type == LUA_TSTRING) {
        std::string const cluster = lua_tostring(lua_state(), -1);
        if (cluster == "auto") {
            new_table.set_cluster_by_geom(true);
        } else if (cluster == "no") {
            new_table.set_cluster_by_geom(false);
        } else {
            throw std::runtime_error{
                "Unknown value '{}' for 'cluster' table option"
                " (use 'auto' or 'no')."_format(cluster)};
        }
    } else if (cluster_type == LUA_TNIL) {
        // ignore
    } else {
        throw std::runtime_error{
            "Unknown value for 'cluster' table option: Must be string."};
    }
    lua_pop(lua_state(), 1);

    // optional "data_tablespace" field
    lua_getfield(lua_state(), -1, "data_tablespace");
    if (lua_isstring(lua_state(), -1)) {
        std::string const tablespace = lua_tostring(lua_state(), -1);
        check_identifier(tablespace, "data_tablespace field");
        new_table.set_data_tablespace(tablespace);
    }
    lua_pop(lua_state(), 1);

    // optional "index_tablespace" field
    lua_getfield(lua_state(), -1, "index_tablespace");
    if (lua_isstring(lua_state(), -1)) {
        std::string const tablespace = lua_tostring(lua_state(), -1);
        check_identifier(tablespace, "index_tablespace field");
        new_table.set_index_tablespace(tablespace);
    }
    lua_pop(lua_state(), 1);

    return new_table;
}

void output_flex_t::setup_id_columns(flex_table_t *table)
{
    assert(table);
    lua_getfield(lua_state(), -1, "ids");
    if (lua_type(lua_state(), -1) != LUA_TTABLE) {
        log_warn("Table '{}' doesn't have an id column. Two-stage"
                 " processing, updates and expire will not work!",
                 table->name());
        lua_pop(lua_state(), 1); // ids
        return;
    }

    std::string const type{
        luaX_get_table_string(lua_state(), "type", -1, "The ids field")};

    if (type == "node") {
        table->set_id_type(osmium::item_type::node);
    } else if (type == "way") {
        table->set_id_type(osmium::item_type::way);
    } else if (type == "relation") {
        table->set_id_type(osmium::item_type::relation);
    } else if (type == "area") {
        table->set_id_type(osmium::item_type::area);
    } else if (type == "any") {
        table->set_id_type(osmium::item_type::undefined);
        lua_getfield(lua_state(), -2, "type_column");
        if (lua_isstring(lua_state(), -1)) {
            std::string const column_name =
                lua_tolstring(lua_state(), -1, nullptr);
            check_identifier(column_name, "column names");
            auto &column = table->add_column(column_name, "id_type", "");
            column.set_not_null();
        } else if (!lua_isnil(lua_state(), -1)) {
            throw std::runtime_error{"type_column must be a string or nil."};
        }
        lua_pop(lua_state(), 1); // type_column
    } else {
        throw std::runtime_error{"Unknown ids type: {}."_format(type)};
    }

    std::string const name =
        luaX_get_table_string(lua_state(), "id_column", -2, "The ids field");
    check_identifier(name, "column names");

    auto &column = table->add_column(name, "id_num", "");
    column.set_not_null();
    lua_pop(lua_state(), 3); // id_column, type, ids
}

void output_flex_t::setup_flex_table_columns(flex_table_t *table)
{
    assert(table);
    lua_getfield(lua_state(), -1, "columns");
    if (lua_type(lua_state(), -1) != LUA_TTABLE) {
        throw std::runtime_error{
            "No columns defined for table '{}'."_format(table->name())};
    }

    std::size_t num_columns = 0;
    lua_pushnil(lua_state());
    while (lua_next(lua_state(), -2) != 0) {
        if (!lua_isnumber(lua_state(), -2)) {
            throw std::runtime_error{
                "The 'columns' field must contain an array."};
        }
        if (!lua_istable(lua_state(), -1)) {
            throw std::runtime_error{
                "The entries in the 'columns' array must be tables."};
        }

        char const *const type = luaX_get_table_string(lua_state(), "type", -1,
                                                       "Column entry", "text");
        char const *const name =
            luaX_get_table_string(lua_state(), "column", -2, "Column entry");
        check_identifier(name, "column names");
        char const *const sql_type = luaX_get_table_string(
            lua_state(), "sql_type", -3, "Column entry", "");

        auto &column = table->add_column(name, type, sql_type);

        column.set_not_null(luaX_get_table_bool(lua_state(), "not_null", -4,
                                                "Entry 'not_null'", false));

        column.set_create_only(luaX_get_table_bool(
            lua_state(), "create_only", -5, "Entry 'create_only'", false));

        lua_getfield(lua_state(), -6, "projection");
        if (!lua_isnil(lua_state(), -1)) {
            if (column.is_geometry_column() ||
                column.type() == table_column_type::area) {
                column.set_projection(lua_tostring(lua_state(), -1));
            } else {
                throw std::runtime_error{
                    "Projection can only be set on geometry and area columns."};
            }
        }

        // stack has: projection, create_only, not_null, sql_type, column,
        //            type, table
        lua_pop(lua_state(), 7);
        ++num_columns;
    }

    if (num_columns == 0) {
        throw std::runtime_error{
            "No columns defined for table '{}'."_format(table->name())};
    }
}

int output_flex_t::app_define_table()
{
    if (m_calling_context != calling_context::main) {
        throw std::runtime_error{
            "Database tables have to be defined in the"
            " main Lua code, not in any of the callbacks."};
    }

    luaL_checktype(lua_state(), 1, LUA_TTABLE);

    auto &new_table = create_flex_table();
    setup_id_columns(&new_table);
    setup_flex_table_columns(&new_table);

    void *ptr = lua_newuserdata(lua_state(), sizeof(std::size_t));
    std::size_t *num = new (ptr) std::size_t{};
    *num = m_tables->size() - 1;
    luaL_getmetatable(lua_state(), osm2pgsql_table_name);
    lua_setmetatable(lua_state(), -2);

    return 1;
}

// Check that the first element on the Lua stack is an osm2pgsql.Table
// parameter and return its internal table index.
static std::size_t table_idx_from_param(lua_State *lua_state)
{
    void const *const user_data = lua_touserdata(lua_state, 1);

    if (user_data == nullptr || !lua_getmetatable(lua_state, 1)) {
        throw std::runtime_error{
            "First parameter must be of type osm2pgsql.Table."};
    }

    luaL_getmetatable(lua_state, osm2pgsql_table_name);
    if (!lua_rawequal(lua_state, -1, -2)) {
        throw std::runtime_error{
            "First parameter must be of type osm2pgsql.Table."};
    }
    lua_pop(lua_state, 2);

    return *static_cast<std::size_t const *>(user_data);
}

// Get the flex table that is as first parameter on the Lua stack.
flex_table_t const &output_flex_t::get_table_from_param()
{
    if (lua_gettop(lua_state()) != 1) {
        throw std::runtime_error{
            "Need exactly one parameter of type osm2pgsql.table."};
    }

    auto const &table = m_tables->at(table_idx_from_param(lua_state()));
    lua_remove(lua_state(), 1);
    return table;
}

int output_flex_t::table_tostring()
{
    auto const &table = get_table_from_param();

    std::string const str{"osm2pgsql.table[{}]"_format(table.name())};
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
            "Need two parameters: The osm2pgsql.table and the row data."};
    }

    auto &table_connection =
        m_table_connections.at(table_idx_from_param(lua_state()));
    auto const &table = table_connection.table();

    // It there is a second parameter, it must be a Lua table.
    if (num_params == 2) {
        luaL_checktype(lua_state(), 2, LUA_TTABLE);
    }
    lua_remove(lua_state(), 1);

    if (m_calling_context == calling_context::process_node) {
        if (!table.matches_type(osmium::item_type::node)) {
            throw std::runtime_error{
                "Trying to add node to table '{}'."_format(table.name())};
        }
        add_row(&table_connection, *m_context_node);
    } else if (m_calling_context == calling_context::process_way) {
        if (!table.matches_type(osmium::item_type::way)) {
            throw std::runtime_error{
                "Trying to add way to table '{}'."_format(table.name())};
        }
        add_row(&table_connection, m_way_cache.get());
    } else if (m_calling_context == calling_context::process_relation) {
        if (!table.matches_type(osmium::item_type::relation)) {
            throw std::runtime_error{
                "Trying to add relation to table '{}'."_format(table.name())};
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
            throw std::runtime_error{
                "Trying to add node to table '{}'."_format(table.name())};
        }
        return *m_context_node;
    }

    if (m_calling_context == calling_context::process_way) {
        if (!table.matches_type(osmium::item_type::way)) {
            throw std::runtime_error{
                "Trying to add way to table '{}'."_format(table.name())};
        }
        return m_way_cache.get();
    }

    assert(m_calling_context == calling_context::process_relation);

    if (!table.matches_type(osmium::item_type::relation)) {
        throw std::runtime_error{
            "Trying to add relation to table '{}'."_format(table.name())};
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
            "Need two parameters: The osm2pgsql.table and the row data."};
    }

    // The first parameter is the table object.
    auto &table_connection =
        m_table_connections.at(table_idx_from_param(lua_state()));

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
                write_column(copy_mgr, column);
            }
        }
    } catch (not_null_exception const &e) {
        copy_mgr->rollback_line();
        lua_pushboolean(lua_state(), false);
        lua_pushstring(lua_state(), "null value in not null column.");
        lua_pushstring(lua_state(), e.column().name().c_str());
        push_osm_object_to_lua_stack(lua_state(), object,
                                     get_options()->extra_attributes);
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

static std::unique_ptr<geom_transform_t>
get_transform(lua_State *lua_state, flex_table_column_t const &column)
{
    assert(lua_state);
    assert(lua_gettop(lua_state) == 1);

    std::unique_ptr<geom_transform_t> transform{};

    lua_getfield(lua_state, -1, column.name().c_str());
    int const ltype = lua_type(lua_state, -1);

    // Field not set, return null transform
    if (ltype == LUA_TNIL) {
        lua_pop(lua_state, 1); // geom field
        return transform;
    }

    // Field set to anything but a Lua table is not allowed
    if (ltype != LUA_TTABLE) {
        lua_pop(lua_state, 1); // geom field
        throw std::runtime_error{
            "Invalid geometry transformation for column '{}'."_format(
                column.name())};
    }

    lua_getfield(lua_state, -1, "create");
    char const *create_type = lua_tostring(lua_state, -1);
    if (create_type == nullptr) {
        throw std::runtime_error{
            "Missing geometry transformation for column '{}'."_format(
                column.name())};
    }

    transform = create_geom_transform(create_type);
    lua_pop(lua_state, 1); // 'create' field
    init_geom_transform(transform.get(), lua_state);
    if (!transform->is_compatible_with(column.type())) {
        throw std::runtime_error{
            "Geometry transformation is not compatible "
            "with column type '{}'."_format(column.type_name())};
    }

    lua_pop(lua_state, 1); // geom field

    return transform;
}

static geom_transform_t const *
get_default_transform(flex_table_column_t const &column,
                      osmium::item_type object_type)
{
    static geom_transform_point_t const default_transform_node_to_point{};
    static geom_transform_line_t const default_transform_way_to_line{};
    static geom_transform_area_t const default_transform_way_to_area{};

    switch (object_type) {
    case osmium::item_type::node:
        if (column.type() == table_column_type::point) {
            return &default_transform_node_to_point;
        }
        break;
    case osmium::item_type::way:
        if (column.type() == table_column_type::linestring) {
            return &default_transform_way_to_line;
        }
        if (column.type() == table_column_type::polygon) {
            return &default_transform_way_to_area;
        }
        break;
    default:
        break;
    }

    throw std::runtime_error{
        "Missing geometry transformation for column '{}'."_format(
            column.name())};
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
        throw std::runtime_error{
            "Table '{}' has more than one geometry column."
            " This is not allowed with 'add_row()'."
            " Maybe use 'insert()' instead?"_format(table.name())};
    }

    osmid_t const id = table.map_id(object.type(), object.id());

    if (!table.has_geom_column()) {
        write_row(table_connection, object.type(), id, {}, 0);
        return;
    }

    // From here we are handling the case where the table has a geometry
    // column. In this case the second parameter to the Lua function add_row()
    // must be present.
    if (lua_gettop(lua_state()) == 0) {
        throw std::runtime_error{
            "Need two parameters: The osm2pgsql.table and the row data."};
    }

    auto const geom_transform = get_transform(lua_state(), table.geom_column());
    assert(lua_gettop(lua_state()) == 1);

    geom_transform_t const *transform = geom_transform.get();

    if (!transform) {
        transform = get_default_transform(table.geom_column(), object.type());
    }

    auto const &proj = table_connection->proj();
    auto const type = table.geom_column().type();

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
        m_expire.from_geometry(sgeom);
        write_row(table_connection, object.type(), id, sgeom,
                  table.geom_column().srid());
    }
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
        throw std::runtime_error{
            "Failed to execute Lua function 'osm2pgsql.{}':"
            " {}."_format(func.name(), lua_tostring(lua_state(), -1))};
    }

    m_calling_context = calling_context::main;
}

void output_flex_t::get_mutex_and_call_lua_function(
    prepared_lua_function_t func, osmium::OSMObject const &object)
{
    std::lock_guard<std::mutex> guard{lua_mutex};
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

    std::lock_guard<std::mutex> guard{lua_mutex};
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
    lua_pushnil(lua_state());
    while (lua_next(lua_state(), -2) != 0) {
        if (!lua_isnumber(lua_state(), -2)) {
            throw std::runtime_error{
                "Table returned from select_relation_members() contains 'ways' "
                "field, but it isn't an array table."};
        }

        osmid_t const id = lua_tointeger(lua_state(), -1);
        if (id == 0) {
            throw std::runtime_error{
                "Table returned from select_relation_members() contains 'ways' "
                "field, which must contain an array of non-zero integer way "
                "ids."};
        }

        m_stage2_way_ids->set(id);
        lua_pop(lua_state(), 1); // value pushed by lua_next()
    }

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

void output_flex_t::stop()
{
    for (auto &table : m_table_connections) {
        table.task_set(thread_pool().submit([&]() {
            table.stop(get_options()->slim && !get_options()->droptemp,
                       get_options()->append);
        }));
    }

    if (get_options()->expire_tiles_zoom_min > 0) {
        auto const count = output_tiles_to_file(
            m_expire.get_tiles(), get_options()->expire_tiles_filename.c_str(),
            get_options()->expire_tiles_zoom_min,
            get_options()->expire_tiles_zoom);
        log_info("Wrote {} entries to expired tiles list", count);
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

    if (m_expire.enabled() && table_connection->table().has_geom_column()) {
        auto const result = table_connection->get_geom_by_id(type, id);
        expire_from_result(&m_expire, result);
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

void output_flex_t::init_clone()
{
    for (auto &table : m_table_connections) {
        table.connect(get_options()->database_options.conninfo());
        table.prepare();
    }
}

void output_flex_t::start()
{
    for (auto &table : m_table_connections) {
        table.connect(get_options()->database_options.conninfo());
        table.start(get_options()->append);
    }
}

std::shared_ptr<output_t>
output_flex_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &copy_thread) const
{
    return std::make_shared<output_flex_t>(
        mid, m_thread_pool, *get_options(), copy_thread, true, m_lua_state,
        m_process_node, m_process_way, m_process_relation,
        m_select_relation_members, m_tables, m_stage2_way_ids);
}

output_flex_t::output_flex_t(
    std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<thread_pool_t> thread_pool, options_t const &o,
    std::shared_ptr<db_copy_thread_t> const &copy_thread, bool is_clone,
    std::shared_ptr<lua_State> lua_state, prepared_lua_function_t process_node,
    prepared_lua_function_t process_way,
    prepared_lua_function_t process_relation,
    prepared_lua_function_t select_relation_members,
    std::shared_ptr<std::vector<flex_table_t>> tables,
    std::shared_ptr<idset_t> stage2_way_ids)
: output_t(mid, std::move(thread_pool), o), m_tables(std::move(tables)),
  m_stage2_way_ids(std::move(stage2_way_ids)), m_copy_thread(copy_thread),
  m_lua_state(std::move(lua_state)),
  m_expire(o.expire_tiles_zoom, o.expire_tiles_max_bbox, o.projection),
  m_process_node(process_node), m_process_way(process_way),
  m_process_relation(process_relation),
  m_select_relation_members(select_relation_members)
{
    assert(copy_thread);

    if (!is_clone) {
        init_lua(get_options()->style);

        // If the osm2pgsql.select_relation_members() Lua function is defined
        // it means we need two-stage processing which in turn means we need
        // the full ways stored in the middle.
        if (m_select_relation_members) {
            m_output_requirements.full_ways = true;
        }
    }

    if (m_tables->empty()) {
        throw std::runtime_error{
            "No tables defined in Lua config. Nothing to do!"};
    }

    assert(m_table_connections.empty());
    for (auto &table : *m_tables) {
        m_table_connections.emplace_back(&table, m_copy_thread);
    }

    if (is_clone) {
        init_clone();
    }
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
}

void output_flex_t::init_lua(std::string const &filename)
{
    m_lua_state.reset(luaL_newstate(),
                      [](lua_State *state) { lua_close(state); });

    // Set up global lua libs
    luaL_openlibs(lua_state());

    // Set up global "osm2pgsql" object
    lua_newtable(lua_state());

    luaX_add_table_str(lua_state(), "version", get_osm2pgsql_short_version());
    luaX_add_table_str(lua_state(), "mode",
                       get_options()->append ? "append" : "create");
    luaX_add_table_int(lua_state(), "stage", 1);

    std::string dir_path =
        boost::filesystem::path{filename}.parent_path().string();
    if (!dir_path.empty()) {
        dir_path += boost::filesystem::path::preferred_separator;
    }
    luaX_add_table_str(lua_state(), "config_dir", dir_path.c_str());

    luaX_add_table_func(lua_state(), "define_table",
                        lua_trampoline_app_define_table);

    lua_setglobal(lua_state(), "osm2pgsql");

    init_table_class(lua_state());

    // Clean up stack
    lua_settop(lua_state(), 0);

    init_geometry_class(lua_state());

    assert(lua_gettop(lua_state()) == 0);

    // Load compiled in init.lua
    if (luaL_dostring(lua_state(), lua_init())) {
        throw std::runtime_error{"Internal error in Lua setup: {}."_format(
            lua_tostring(lua_state(), -1))};
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
        throw std::runtime_error{"Error loading lua config: {}."_format(
            lua_tostring(lua_state(), -1))};
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
        log_info("Entering stage 1c processing of {} ways..."_format(
            m_stage2_way_ids->size()));
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

        log_info("Creating id indexes took {}"_format(
            util::human_readable_duration(timer.stop())));
    }

    lua_gc(lua_state(), LUA_GCCOLLECT, 0);
    log_debug("Lua program uses {} MBytes",
              lua_gc(lua_state(), LUA_GCCOUNT, 0) / 1024);

    lua_getglobal(lua_state(), "osm2pgsql");
    lua_pushinteger(lua_state(), 2);
    lua_setfield(lua_state(), -2, "stage");
    lua_pop(lua_state(), 1); // osm2pgsql

    m_stage2_way_ids->sort_unique();

    log_info(
        "There are {} ways to reprocess..."_format(m_stage2_way_ids->size()));

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
    auto *opgsql = dynamic_cast<output_flex_t *>(other);
    if (opgsql) {
        m_expire.merge_and_destroy(&opgsql->m_expire);
    }
}
