#ifndef GEOMETRY_PROCESSOR_HPP
#define GEOMETRY_PROCESSOR_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include "geometry-builder.hpp"
#include "osmtypes.hpp"

struct middle_query_t;
struct middle_t;
struct options_t;

struct geometry_processor {
    // factory method for creating various types of geometry processors either by name or by geometry column type
    static std::shared_ptr<geometry_processor> create(const std::string &type,
                                                        const options_t *options);

    virtual ~geometry_processor();

    enum interest {
        interest_NONE     = 0,
        interest_node     = 1,
        interest_way      = 2,
        interest_relation = 4,
        interest_ALL      = 7
    };

    // return bit-mask of the type of elements this processor is
    // interested in.
    unsigned int interests() const;

    // return true if provided intrest is an interest of this processor
    bool interests(unsigned int interested) const;

    // the postgis column type for the kind of geometry (i.e: POINT,
    // LINESTRING, etc...) that this processor outputs
    const std::string &column_type() const;

    // process a node, optionally returning a WKB string describing
    // geometry to be inserted into the table.
    virtual geometry_builder::pg_geom_t process_node(double lat, double lon);

    // process a way
    // position data and optionally returning WKB-encoded geometry
    // for insertion into the table.
    virtual geometry_builder::pg_geom_t process_way(const nodelist_t &nodes);

    // process a way, taking a middle query object to get way and
    // node position data. optionally returns an array of WKB-encoded geometry
    // for insertion into the table.
    virtual geometry_builder::pg_geoms_t process_relation(const multinodelist_t &nodes);

    // returns the SRID of the output geometry.
    int srid() const;

protected:
    // SRID of the geometry output
    const int m_srid;

    // WKT type of the geometry output
    const std::string m_type;

    // mask of elements that this processor is interested in
    const unsigned int m_interests;

    // constructor for use by implementing classes only
    geometry_processor(int srid, const std::string &type, unsigned int interests);
};


//various bits for continuous processing of ways
struct way_helper
{
    way_helper();
    ~way_helper();
    size_t set(const idlist_t &node_ids, const middle_query_t *mid);

    nodelist_t node_cache;
};

//various bits for continuous processing of members of relations
struct relation_helper
{
    relation_helper();
    ~relation_helper();
    size_t set(const memberlist_t *member_list, const middle_t *mid);

    const memberlist_t *members;
    multitaglist_t tags;
    multinodelist_t nodes;
    idlist_t ways;
    rolelist_t roles;
    std::vector<int> superseeded;

private:
    idlist_t input_way_ids;
};

#endif /* GEOMETRY_PROCESSOR_HPP */
