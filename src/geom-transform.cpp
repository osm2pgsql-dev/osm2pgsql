
#include "geom-transform.hpp"
#include "logging.hpp"

#include <osmium/osm.hpp>

#include <cstring>
#include <stdexcept>

bool geom_transform_point_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    return geom_type == table_column_type::point ||
           geom_type == table_column_type::geometry;
}

geom::osmium_builder_t::wkbs_t
geom_transform_point_t::run(geom::osmium_builder_t *builder,
                            osmium::Node const &node) const
{
    assert(builder);

    return {builder->get_wkb_node(node.location())};
}

bool geom_transform_line_t::set_param(char const *name, lua_State *lua_state)
{
    if (std::strcmp(name, "split_at") != 0) {
        return false;
    }

    if (lua_type(lua_state, -1) != LUA_TNUMBER) {
        throw std::runtime_error{
            "The 'split_at' field in a geometry transformation "
            "description must be a number."};
    }
    m_split_at = lua_tonumber(lua_state, -1);

    return true;
}

bool geom_transform_line_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    return geom_type == table_column_type::linestring ||
           geom_type == table_column_type::multilinestring ||
           geom_type == table_column_type::geometry;
}

geom::osmium_builder_t::wkbs_t
geom_transform_line_t::run(geom::osmium_builder_t *builder,
                           osmium::Way *way) const
{
    assert(builder);
    assert(way);

    return builder->get_wkb_line(way->nodes(), m_split_at);
}

geom::osmium_builder_t::wkbs_t
geom_transform_line_t::run(geom::osmium_builder_t *builder,
                           osmium::Relation const & /*relation*/,
                           osmium::memory::Buffer const &buffer) const
{
    assert(builder);

    return builder->get_wkb_multiline(buffer, m_split_at);
}

bool geom_transform_area_t::set_param(char const *name, lua_State *lua_state)
{
    if (std::strcmp(name, "multi") != 0) {
        return false;
    }

    if (lua_type(lua_state, -1) != LUA_TBOOLEAN) {
        throw std::runtime_error{
            "The 'multi' field in a geometry transformation "
            "description must be a boolean."};
    }
    m_multi = lua_toboolean(lua_state, -1);

    return true;
}

bool geom_transform_area_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    if (m_multi) {
        return geom_type == table_column_type::multipolygon ||
               geom_type == table_column_type::geometry;
    }

    return geom_type == table_column_type::polygon ||
           geom_type == table_column_type::geometry;
}

geom::osmium_builder_t::wkbs_t
geom_transform_area_t::run(geom::osmium_builder_t *builder,
                           osmium::Way *way) const
{
    assert(builder);
    assert(way);

    if (!way->is_closed()) {
        return {};
    }

    geom::osmium_builder_t::wkbs_t result;
    result.push_back(builder->get_wkb_polygon(*way));

    if (result.front().empty()) {
        result.clear();
    }

    return result;
}

geom::osmium_builder_t::wkbs_t
geom_transform_area_t::run(geom::osmium_builder_t *builder,
                           osmium::Relation const &relation,
                           osmium::memory::Buffer const &buffer) const
{
    assert(builder);

    return builder->get_wkb_multipolygon(relation, buffer, m_multi);
}

std::unique_ptr<geom_transform_t> create_geom_transform(char const *type)
{
    if (std::strcmp(type, "point") == 0) {
        return std::unique_ptr<geom_transform_t>{new geom_transform_point_t{}};
    }

    if (std::strcmp(type, "line") == 0) {
        return std::unique_ptr<geom_transform_t>{new geom_transform_line_t{}};
    }

    if (std::strcmp(type, "area") == 0) {
        return std::unique_ptr<geom_transform_t>{new geom_transform_area_t{}};
    }

    throw std::runtime_error{
        "Unknown geometry transformation '{}'."_format(type)};
}

void init_geom_transform(geom_transform_t *transform, lua_State *lua_state)
{
    static bool show_warning = true;

    assert(transform);
    assert(lua_state);

    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        char const *const field = lua_tostring(lua_state, -2);
        if (field == nullptr) {
            throw std::runtime_error{"All fields in geometry transformation "
                                     "description must have string keys."};
        }

        if (std::strcmp(field, "create") != 0) {
            if (!transform->set_param(field, lua_state) && show_warning) {
                log_warn("Ignoring unknown field '{}' in geometry "
                         "transformation description.",
                         field);
                show_warning = false;
            }
        }

        lua_pop(lua_state, 1);
    }
}
