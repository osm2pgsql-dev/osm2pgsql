#include <osmium/osm.hpp>

#include "format.hpp"
#include "input-handler.hpp"
#include "logging.hpp"
#include "osmdata.hpp"

input_handler_t::input_handler_t(osmium::Box const &bbox, bool append,
                                 osmdata_t const *osmdata)
: m_data(osmdata), m_bbox(bbox), m_append(append)
{}

void input_handler_t::negative_id_warning()
{
    fmt::print(
        stderr,
        "\nWARNING: The input file contains at least one object with a\n"
        "         negative id. Negative ids are not properly supported\n"
        "         in osm2pgsql (and never were). They will not work in\n"
        "         future versions at all. You can use the osmium tool to\n"
        "         'renumber' your file.\n\n");
    m_issued_warning_negative_id = true;
}

void input_handler_t::node(osmium::Node const &node)
{
    if (node.id() < 0 && !m_issued_warning_negative_id) {
        negative_id_warning();
    }

    if (m_type != osmium::item_type::node) {
        m_type = osmium::item_type::node;
        m_data->flush();
    }

    if (node.deleted()) {
        if (!m_append) {
            throw std::runtime_error{"Input file contains deleted objects but "
                                     "you are not in append mode."};
        }
        m_data->node_delete(node.id());
    } else {
        // if the node is not valid, then node.location.lat/lon() can throw.
        // we probably ought to treat invalid locations as if they were
        // deleted and ignore them.
        if (!node.location().valid()) {
            log_warn("Ignored invalid location on node {} (version {})",
                     node.id(), node.version());
            return;
        }

        if (!m_bbox.valid() || m_bbox.contains(node.location())) {
            if (m_append) {
                m_data->node_modify(node);
            } else {
                m_data->node_add(node);
            }
            m_progress.add_node(node.id());
        }
    }
}

void input_handler_t::way(osmium::Way &way)
{
    if (way.id() < 0 && !m_issued_warning_negative_id) {
        negative_id_warning();
    }

    if (m_type != osmium::item_type::way) {
        m_type = osmium::item_type::way;
        m_data->flush();
    }

    if (way.deleted()) {
        if (!m_append) {
            throw std::runtime_error{"Input file contains deleted objects but "
                                     "you are not in append mode."};
        }
        m_data->way_delete(way.id());
    } else {
        if (m_append) {
            m_data->way_modify(&way);
        } else {
            m_data->way_add(&way);
        }
    }
    m_progress.add_way(way.id());
}

void input_handler_t::relation(osmium::Relation const &rel)
{
    if (rel.id() < 0 && !m_issued_warning_negative_id) {
        negative_id_warning();
    }

    if (m_type != osmium::item_type::relation) {
        m_type = osmium::item_type::relation;
        m_data->flush();
    }

    if (rel.deleted()) {
        if (!m_append) {
            throw std::runtime_error{"Input file contains deleted objects but "
                                     "you are not in append mode."};
        }
        m_data->relation_delete(rel.id());
    } else {
        if (rel.members().size() > 32767) {
            return;
        }
        if (m_append) {
            m_data->relation_modify(rel);
        } else {
            m_data->relation_add(rel);
        }
    }
    m_progress.add_rel(rel.id());
}
