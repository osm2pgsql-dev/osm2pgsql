/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-geom.hpp"
#include "flex-write.hpp"
#include "geom-functions.hpp"
#include "json-writer.hpp"
#include "wkb.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

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

static void write_json(json_writer_t *writer, lua_State *lua_state,
                       table_register_type *tables);

static void write_json_table(json_writer_t *writer, lua_State *lua_state,
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
        writer->start_array();
        lua_pushnil(lua_state);
        while (lua_next(lua_state, -2) != 0) {
            write_json(writer, lua_state, tables);
            writer->next();
            lua_pop(lua_state, 1);
        }
        writer->end_array();
    } else {
        writer->start_object();
        lua_pushnil(lua_state);
        while (lua_next(lua_state, -2) != 0) {
            int const ltype_key = lua_type(lua_state, -2);
            if (ltype_key != LUA_TSTRING) {
                throw std::runtime_error{
                    "Incorrect data type '{}' as key."_format(
                        lua_typename(lua_state, ltype_key))};
            }
            char const *const key = lua_tostring(lua_state, -2);
            writer->key(key);
            write_json(writer, lua_state, tables);
            writer->next();
            lua_pop(lua_state, 1);
        }
        writer->end_object();
    }
}

static void write_json_number(json_writer_t *writer, lua_State *lua_state)
{
#if LUA_VERSION_NUM >= 503
    int okay = 0;
    auto const num = lua_tointegerx(lua_state, -1, &okay);
    if (okay) {
        writer->number(num);
    } else {
        writer->number(lua_tonumber(lua_state, -1));
    }
#else
    double const num = lua_tonumber(lua_state, -1);
    double intpart = 0.0;
    if (std::modf(num, &intpart) == 0.0) {
        writer->number(static_cast<int64_t>(num));
    } else {
        writer->number(num);
    }
#endif
}

static void write_json(json_writer_t *writer, lua_State *lua_state,
                       table_register_type *tables)
{
    assert(writer);
    assert(lua_state);

    int const ltype = lua_type(lua_state, -1);
    switch (ltype) {
    case LUA_TNIL:
        writer->null();
        break;
    case LUA_TBOOLEAN:
        writer->boolean(lua_toboolean(lua_state, -1) != 0);
        break;
    case LUA_TNUMBER:
        write_json_number(writer, lua_state);
        break;
    case LUA_TSTRING:
        writer->string(lua_tostring(lua_state, -1));
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

void flex_write_column(lua_State *lua_state,
                       db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                       flex_table_column_t const &column, expire_tiles *expire)
{
    // If there is nothing on the Lua stack, then the Lua function add_row()
    // was called without a table parameter. In that case this column will
    // be set to NULL.
    if (lua_gettop(lua_state) == 0) {
        write_null(copy_mgr, column);
        return;
    }

    lua_getfield(lua_state, -1, column.name().c_str());
    int const ltype = lua_type(lua_state, -1);

    // Certain Lua types can never be added to the database
    if (ltype == LUA_TFUNCTION || ltype == LUA_TTHREAD) {
        throw std::runtime_error{
            "Can not add Lua objects of type function or thread."};
    }

    // A Lua nil value is always translated to a database NULL
    if (ltype == LUA_TNIL) {
        write_null(copy_mgr, column);
        lua_pop(lua_state, 1);
        return;
    }

    if (column.type() == table_column_type::text) {
        auto const *const str = lua_tolstring(lua_state, -1, nullptr);
        if (!str) {
            throw std::runtime_error{
                "Invalid type '{}' for text column."_format(
                    lua_typename(lua_state, ltype))};
        }
        copy_mgr->add_column(str);
    } else if (column.type() == table_column_type::boolean) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(lua_state, -1) != 0);
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(lua_tonumber(lua_state, -1) != 0);
            break;
        case LUA_TSTRING:
            write_boolean(copy_mgr, column,
                          lua_tolstring(lua_state, -1, nullptr));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for boolean column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::int2) {
        if (ltype == LUA_TNUMBER) {
            int64_t const value = lua_tointeger(lua_state, -1);
            if (value >= std::numeric_limits<int16_t>::min() &&
                value <= std::numeric_limits<int16_t>::max()) {
                copy_mgr->add_column(value);
            } else {
                write_null(copy_mgr, column);
            }
        } else if (ltype == LUA_TSTRING) {
            write_integer<int16_t>(copy_mgr, column,
                                   lua_tolstring(lua_state, -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state, -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int2 column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::int4) {
        if (ltype == LUA_TNUMBER) {
            int64_t const value = lua_tointeger(lua_state, -1);
            if (value >= std::numeric_limits<int32_t>::min() &&
                value <= std::numeric_limits<int32_t>::max()) {
                copy_mgr->add_column(value);
            } else {
                write_null(copy_mgr, column);
            }
        } else if (ltype == LUA_TSTRING) {
            write_integer<int32_t>(copy_mgr, column,
                                   lua_tolstring(lua_state, -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state, -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int4 column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::int8) {
        if (ltype == LUA_TNUMBER) {
            copy_mgr->add_column(lua_tointeger(lua_state, -1));
        } else if (ltype == LUA_TSTRING) {
            write_integer<int64_t>(copy_mgr, column,
                                   lua_tolstring(lua_state, -1, nullptr));
        } else if (ltype == LUA_TBOOLEAN) {
            copy_mgr->add_column(lua_toboolean(lua_state, -1));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for int8 column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::real) {
        if (ltype == LUA_TNUMBER) {
            copy_mgr->add_column(lua_tonumber(lua_state, -1));
        } else if (ltype == LUA_TSTRING) {
            write_double(copy_mgr, column,
                         lua_tolstring(lua_state, -1, nullptr));
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for real column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.type() == table_column_type::hstore) {
        if (ltype == LUA_TTABLE) {
            copy_mgr->new_hash();

            lua_pushnil(lua_state);
            while (lua_next(lua_state, -2) != 0) {
                char const *const key = lua_tostring(lua_state, -2);
                char const *const val = lua_tostring(lua_state, -1);
                if (key == nullptr) {
                    int const ltype_key = lua_type(lua_state, -2);
                    throw std::runtime_error{
                        "NULL key for hstore. Possibly this is due to"
                        " an incorrect data type '{}' as key."_format(
                            lua_typename(lua_state, ltype_key))};
                }
                if (val == nullptr) {
                    int const ltype_value = lua_type(lua_state, -1);
                    throw std::runtime_error{
                        "NULL value for hstore. Possibly this is due to"
                        " an incorrect data type '{}' for key '{}'."_format(
                            lua_typename(lua_state, ltype_value), key)};
                }
                copy_mgr->add_hash_elem(key, val);
                lua_pop(lua_state, 1);
            }

            copy_mgr->finish_hash();
        } else {
            throw std::runtime_error{
                "Invalid type '{}' for hstore column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if ((column.type() == table_column_type::json) ||
               (column.type() == table_column_type::jsonb)) {
        json_writer_t writer;
        table_register_type tables;
        write_json(&writer, lua_state, &tables);
        copy_mgr->add_column(writer.json());
    } else if (column.type() == table_column_type::direction) {
        switch (ltype) {
        case LUA_TBOOLEAN:
            copy_mgr->add_column(lua_toboolean(lua_state, -1));
            break;
        case LUA_TNUMBER:
            copy_mgr->add_column(sgn(lua_tonumber(lua_state, -1)));
            break;
        case LUA_TSTRING:
            write_direction(copy_mgr, column,
                            lua_tolstring(lua_state, -1, nullptr));
            break;
        default:
            throw std::runtime_error{
                "Invalid type '{}' for direction column."_format(
                    lua_typename(lua_state, ltype))};
        }
    } else if (column.is_geometry_column()) {
        // If this is a geometry column, the Lua function 'insert()' was
        // called, because for 'add_row()' geometry columns are handled
        // earlier and 'write_column()' is not called.
        if (ltype == LUA_TUSERDATA) {
            auto const *const geom = unpack_geometry(lua_state, -1);
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
                    expire->from_geometry_if_3857(*geom);
                    copy_mgr->add_hex_geom(geom_to_ewkb(*geom, wrap_multi));
                } else {
                    auto const proj =
                        reprojection::create_projection(column.srid());
                    auto const tgeom = geom::transform(*geom, *proj);
                    expire->from_geometry_if_3857(tgeom);
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

    lua_pop(lua_state, 1);
}

void flex_write_row(lua_State *lua_state, table_connection_t *table_connection,
                    osmium::item_type id_type, osmid_t id,
                    geom::geometry_t const &geom, int srid,
                    expire_tiles *expire)
{
    assert(table_connection);
    table_connection->new_line();
    auto *copy_mgr = table_connection->copy_mgr();

    geom::geometry_t projected_geom;
    geom::geometry_t const *output_geom = &geom;
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
                    auto const mproj =
                        reprojection::create_projection(column.srid());
                    area = geom::area(geom::transform(geom, *mproj));
                }
                copy_mgr->add_column(area);
            }
        } else {
            flex_write_column(lua_state, copy_mgr, column, expire);
        }
    }

    copy_mgr->finish_line();
}
