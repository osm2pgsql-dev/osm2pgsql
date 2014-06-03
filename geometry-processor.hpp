#ifndef GEOMETRY_PROCESSOR_HPP
#define GEOMETRY_PROCESSOR_HPP

#include "osmtypes.hpp"
#include "middle.hpp"
#include <string>
#include <boost/optional.hpp>

struct geometry_processor {
    // type to represent an optional return of WKT-encoded geometry
    typedef boost::optional<std::string> maybe_wkt_t;

    static boost::shared_ptr<geometry_processor>
    create(const std::string &type, const options_t *options);

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
    interest interests() const;

    // the postgis column type for the kind of geometry (i.e: POINT,
    // LINESTRING, etc...) that this processor outputs
    const std::string &column_type() const;

    // process a node, optionally returning a WKT string describing
    // geometry to be inserted into the table.
    virtual maybe_wkt_t process_node(double lat, double lon);
    
    // process a way, taking a middle query object to get node
    // position data and optionally returning WKT-encoded geometry
    // for insertion into the table.
    virtual maybe_wkt_t process_way(osmid_t *nodes, int node_count,
                                    const middle_query_t *mid);
    
    // process a way, taking a middle query object to get way and
    // node position data. optionally returns a WKT-encoded geometry
    // for insertion into the table.
    virtual maybe_wkt_t process_relation(struct member *members, int member_count,
                                         const middle_query_t *mid);

    // returns the SRID of the output geometry.
    int srid() const;

protected:
    // SRID of the geometry output
    const int m_srid;

    // WKT type of the geometry output
    const std::string m_type;

    // type of elements that this processor is interested in
    const interest m_interests;

    // constructor for use by implementing classes only
    geometry_processor(int srid, const std::string &type, interest interests);
};

#endif /* GEOMETRY_PROCESSOR_HPP */
