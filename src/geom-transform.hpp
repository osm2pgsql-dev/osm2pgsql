#ifndef OSM2PGSQL_GEOM_TRANSFORM_HPP
#define OSM2PGSQL_GEOM_TRANSFORM_HPP

#include "flex-table-column.hpp"
#include "osmium-builder.hpp"

#include <osmium/fwd.hpp>

extern "C"
{
#include <lua.h>
}

#include <memory>

/**
 * Abstract base class for geometry transformations from nodes, ways, or
 * relations to simple feature type geometries.
 */
class geom_transform_t
{
public:
    virtual ~geom_transform_t() = default;

    virtual bool set_param(char const * /*name*/, lua_State * /*lua_state*/)
    {
        return false;
    }

    virtual bool is_compatible_with(table_column_type geom_type) const
        noexcept = 0;

    virtual geom::osmium_builder_t::wkbs_t
    run(geom::osmium_builder_t * /*builder*/,
        osmium::Node const & /*node*/) const
    {
        return {};
    }

    virtual geom::osmium_builder_t::wkbs_t
    run(geom::osmium_builder_t * /*builder*/, osmium::Way * /*way*/) const
    {
        return {};
    }

    virtual geom::osmium_builder_t::wkbs_t
    run(geom::osmium_builder_t * /*builder*/,
        osmium::Relation const & /*relation*/,
        osmium::memory::Buffer const & /*buffer*/) const
    {
        return {};
    }

}; // class geom_transform_t

class geom_transform_point_t : public geom_transform_t
{
public:
    bool is_compatible_with(table_column_type geom_type) const
        noexcept override;

    geom::osmium_builder_t::wkbs_t run(geom::osmium_builder_t *builder,
                                       osmium::Node const &node) const override;

}; // class geom_transform_point_t

class geom_transform_line_t : public geom_transform_t
{
public:
    bool set_param(char const *name, lua_State *lua_state) override;

    bool is_compatible_with(table_column_type geom_type) const
        noexcept override;

    geom::osmium_builder_t::wkbs_t run(geom::osmium_builder_t *builder,
                                       osmium::Way *way) const override;

    geom::osmium_builder_t::wkbs_t
    run(geom::osmium_builder_t *builder, osmium::Relation const &relation,
        osmium::memory::Buffer const &buffer) const override;

private:
    double m_split_at = 0.0;

}; // class geom_transform_line_t

class geom_transform_area_t : public geom_transform_t
{
public:
    bool set_param(char const *name, lua_State *lua_state) override;

    bool is_compatible_with(table_column_type geom_type) const
        noexcept override;

    geom::osmium_builder_t::wkbs_t run(geom::osmium_builder_t *builder,
                                       osmium::Way *way) const override;

    geom::osmium_builder_t::wkbs_t
    run(geom::osmium_builder_t *builder, osmium::Relation const &relation,
        osmium::memory::Buffer const &buffer) const override;

private:
    bool m_multi = false;

}; // class geom_transform_area_t

std::unique_ptr<geom_transform_t> create_geom_transform(char const *type);

void init_geom_transform(geom_transform_t *transform, lua_State *lua_state);

#endif // OSM2PGSQL_GEOM_TRANSFORM_HPP
