#ifndef OSM2PGSQL_GEOMETRY_PROCESSOR_HPP
#define OSM2PGSQL_GEOMETRY_PROCESSOR_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <osmium/memory/buffer.hpp>

#include "osmium-builder.hpp"
#include "osmtypes.hpp"
#include "tagtransform.hpp"

struct middle_query_t;
class options_t;
class reprojection;

struct geometry_processor
{
    using wkb_t = geom::osmium_builder_t::wkb_t;
    using wkbs_t = geom::osmium_builder_t::wkbs_t;
    // factory method for creating various types of geometry processors either by name or by geometry column type
    static std::shared_ptr<geometry_processor> create(std::string const &type,
                                                      options_t const *options);

    virtual ~geometry_processor();

    enum interest
    {
        interest_NONE = 0,
        interest_node = 1,
        interest_way = 2,
        interest_relation = 4,
        interest_ALL = 7
    };

    // return bit-mask of the type of elements this processor is
    // interested in.
    unsigned int interests() const noexcept;

    // return true if provided intrest is an interest of this processor
    bool interests(unsigned int interested) const noexcept;

    // the postgis column type for the kind of geometry (i.e: POINT,
    // LINESTRING, etc...) that this processor outputs
    std::string const &column_type() const noexcept;

    // process a node, optionally returning a WKB string describing
    // geometry to be inserted into the table.
    virtual wkb_t process_node(osmium::Location const &loc,
                               geom::osmium_builder_t *builder);

    // process a way
    // position data and optionally returning WKB-encoded geometry
    // for insertion into the table.
    virtual wkb_t process_way(osmium::Way const &way,
                              geom::osmium_builder_t *builder);

    // process a way, taking a middle query object to get way and
    // node position data. optionally returns an array of WKB-encoded geometry
    // for insertion into the table.
    virtual wkbs_t process_relation(osmium::Relation const &rel,
                                    osmium::memory::Buffer const &ways,
                                    geom::osmium_builder_t *builder);

    // returns the SRID of the output geometry.
    int srid() const noexcept;

protected:
    // SRID of the geometry output
    int const m_srid;

    // WKT type of the geometry output
    std::string const m_type;

    // mask of elements that this processor is interested in
    unsigned int const m_interests;

    // constructor for use by implementing classes only
    geometry_processor(int srid, std::string const &type,
                       unsigned int interests);
};

//various bits for continuous processing of members of relations
class relation_helper
{
public:
    relation_helper();

    size_t set(osmium::Relation const &rel, middle_query_t const *mid);
    void add_way_locations(middle_query_t const *mid);

    rolelist_t roles;
    osmium::memory::Buffer data;
};

#endif // OSM2PGSQL_GEOMETRY_PROCESSOR_HPP
