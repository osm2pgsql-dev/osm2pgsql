#include "check-order.hpp"
#include "format.hpp"

#include <osmium/osm/relation.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>

void check_order_t::warning(char const *msg, osmid_t id)
{
    fmt::print(stderr,
               "\nWARNING: {}: {}\n"
               "         Unordered input files do not work correctly in all\n"
               "         cases. Future versions of osm2pgsql will require\n"
               "         ordered files. Use the 'sort' command of osmium tool\n"
               "         to sort them first.\n\n",
               msg, id);
    m_issued_warning = true;
}

void check_order_t::node(const osmium::Node &node)
{
    if (m_issued_warning) {
        return;
    }

    if (m_max_way_id > std::numeric_limits<osmid_t>::min()) {
        warning("Found a node after a way", node.id());
    }
    if (m_max_relation_id > std::numeric_limits<osmid_t>::min()) {
        warning("Found a node after a relation", node.id());
    }

    if (m_max_node_id == node.id()) {
        warning("Node ID twice in input. Maybe you are using a history or "
                "non-simplified change file?",
                node.id());
    }
    if (node.id() < m_max_node_id) {
        warning("Node IDs out of order", node.id());
    }
    m_max_node_id = node.id();
}

void check_order_t::way(const osmium::Way &way)
{
    if (m_issued_warning) {
        return;
    }

    if (m_max_relation_id > std::numeric_limits<osmid_t>::min()) {
        warning("Found a way after a relation", way.id());
    }

    if (m_max_way_id == way.id()) {
        warning("Way ID twice in input. Maybe you are using a history or "
                "non-simplified change file?",
                way.id());
    }
    if (way.id() < m_max_way_id) {
        warning("Way IDs out of order", way.id());
    }
    m_max_way_id = way.id();
}

void check_order_t::relation(const osmium::Relation &relation)
{
    if (m_issued_warning) {
        return;
    }

    if (m_max_relation_id == relation.id()) {
        warning("Relation ID twice in input. Maybe you are using a history or "
                "non-simplified change file?",
                relation.id());
    }
    if (relation.id() < m_max_relation_id) {
        warning("Relation IDs out of order", relation.id());
    }
    m_max_relation_id = relation.id();
}
