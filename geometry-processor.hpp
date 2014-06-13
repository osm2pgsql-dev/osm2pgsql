#ifndef GEOMETRY_PROCESSOR_HPP
#define GEOMETRY_PROCESSOR_HPP

#include "osmtypes.hpp"
#include "middle.hpp"
#include "geometry-builder.hpp"
#include <string>
#include <boost/optional.hpp>

struct geometry_processor {
    // factory method for creating various types of geometry processors
    static boost::shared_ptr<geometry_processor> create(const std::string &type,
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

    // process a node, optionally returning a WKT string describing
    // geometry to be inserted into the table.
    virtual geometry_builder::maybe_wkt_t process_node(double lat, double lon);
    
    // process a way, taking a middle query object to get node
    // position data and optionally returning WKT-encoded geometry
    // for insertion into the table.
    virtual geometry_builder::maybe_wkt_t process_way(const osmid_t *node_ids, const size_t node_count, const middle_query_t *mid);
    
    // process a way
    // position data and optionally returning WKT-encoded geometry
    // for insertion into the table.
    virtual geometry_builder::maybe_wkt_t process_way(const osmNode *nodes, const size_t node_count);

    // process a way, taking a middle query object to get way and
    // node position data. optionally returns a WKT-encoded geometry
    // for insertion into the table.
    virtual geometry_builder::maybe_wkts_t process_relation(const osmNode * const * nodes, const int* node_counts);

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

#endif /* GEOMETRY_PROCESSOR_HPP */
