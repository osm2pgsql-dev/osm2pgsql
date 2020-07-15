#ifndef OSM2PGSQL_CHECK_ORDER_HPP
#define OSM2PGSQL_CHECK_ORDER_HPP

#include "osmtypes.hpp"

#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>

#include <limits>

/**
 * Handler that can be used to check that an OSM file is ordered
 * correctly. Ordered in this case refers to the usual order in OSM
 * files: First nodes in the order of their IDs, then ways in the order
 * of their IDs, then relations in the order or their IDs.
 *
 * IDs have to be unique for each type. This check will fail for
 * history files.
 */
class check_order_t : public osmium::handler::Handler
{

public:
    void node(const osmium::Node &node);

    void way(const osmium::Way &way);

    void relation(const osmium::Relation &relation);

private:
    void warning(char const *msg, osmid_t id);

    osmid_t m_max_node_id = std::numeric_limits<osmid_t>::min();
    osmid_t m_max_way_id = std::numeric_limits<osmid_t>::min();
    osmid_t m_max_relation_id = std::numeric_limits<osmid_t>::min();

    bool m_issued_warning = false;
}; // class check_order_t

#endif // OSM2PGSQL_CHECK_ORDER_HPP
