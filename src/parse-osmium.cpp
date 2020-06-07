/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
#-----------------------------------------------------------------------------
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#include "parse-osmium.hpp"
#include "format.hpp"
#include "osmdata.hpp"

#include <osmium/osm.hpp>

void parse_stats_t::update(parse_stats_t const &other)
{
    m_node += other.m_node;
    m_way += other.m_way;
    m_rel += other.m_rel;
}

void parse_stats_t::print_summary() const
{
    std::time_t const now = std::time(nullptr);

    fmt::print(stderr, "Node stats: total({}), max({}) in {}s\n", m_node.count,
               m_node.max, nodes_time(now));
    fmt::print(stderr, "Way stats: total({}), max({}) in {}s\n", m_way.count,
               m_way.max, ways_time(now));
    fmt::print(stderr, "Relation stats: total({}), max({}) in {}s\n",
               m_rel.count, m_rel.max, rels_time(now));
}

void parse_stats_t::print_status(std::time_t now) const
{
    fmt::print(
        stderr,
        "\rProcessing: Node({}k {:.1f}k/s) Way({}k {:.2f}k/s)"
        " Relation({} {:.1f}/s)",
        m_node.count_k(), count_per_second(m_node.count_k(), nodes_time(now)),
        m_way.count_k(), count_per_second(m_way.count_k(), ways_time(now)),
        m_rel.count, count_per_second(m_rel.count, rels_time(now)));
}

void parse_stats_t::possibly_print_status()
{
    std::time_t const now = std::time(nullptr);

    if (m_last_print_time >= now) {
        return;
    }
    m_last_print_time = now;

    print_status(now);
}

parse_osmium_t::parse_osmium_t(osmium::Box const &bbox, bool append,
                               osmdata_t const *osmdata)
: m_data(osmdata), m_bbox(bbox), m_append(append)
{}

void parse_osmium_t::node(osmium::Node const &node)
{
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
            fmt::print(
                stderr,
                "WARNING: Node {} (version {}) has an invalid location and has "
                "been ignored. This is not expected to happen with recent "
                "planet files, so please check that your input is correct.\n",
                node.id(), node.version());

            return;
        }

        if (!m_bbox.valid() || m_bbox.contains(node.location())) {
            if (m_append) {
                m_data->node_modify(node);
            } else {
                m_data->node_add(node);
            }
            m_stats.add_node(node.id());
        }
    }
}

void parse_osmium_t::way(osmium::Way &way)
{
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
    m_stats.add_way(way.id());
}

void parse_osmium_t::relation(osmium::Relation const &rel)
{
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
    m_stats.add_rel(rel.id());
}
