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

#include <boost/format.hpp>

#include "parse-osmium.hpp"
#include "reprojection.hpp"
#include "osmdata.hpp"

#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/osm.hpp>

void parse_stats_t::update(const parse_stats_t &other)
{
    node += other.node;
    way += other.way;
    rel += other.rel;
}


void parse_stats_t::print_summary() const
{
    time_t now = time(nullptr);
    time_t end_nodes = way.start > 0 ? way.start : now;
    time_t end_way = rel.start > 0 ? rel.start : now;
    time_t end_rel = now;

    fprintf(stderr,
            "Node stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            node.count, node.max,
            node.count > 0 ? (int) (end_nodes - node.start) : 0);
    fprintf(stderr,
            "Way stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            way.count, way.max,
            way.count > 0 ? (int) (end_way - way.start) : 0);
    fprintf(stderr,
            "Relation stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            rel.count, rel.max,
            rel.count > 0 ? (int) (end_rel - rel.start) : 0);
}

void parse_stats_t::print_status() const
{
    time_t now = time(nullptr);
    time_t end_nodes = way.start > 0 ? way.start : now;
    time_t end_way = rel.start > 0 ? rel.start : now;
    time_t end_rel = now;
    fprintf(stderr,
            "\rProcessing: Node(%" PRIdOSMID "k %.1fk/s) Way(%" PRIdOSMID "k %.2fk/s) Relation(%" PRIdOSMID " %.2f/s)",
            node.count / 1000,
            (double) node.count / 1000.0 / ((int) (end_nodes - node.start) > 0 ? (double) (end_nodes - node.start) : 1.0),
            way.count / 1000,
            way.count > 0 ? (double) way.count / 1000.0 / ((double) (end_way - way.start) > 0.0 ? (double) (end_way - way.start) : 1.0) : 0.0, rel.count,
            rel.count > 0 ? (double) rel.count / ((double) (end_rel - rel.start) > 0.0 ? (double) (end_rel - rel.start) : 1.0) : 0.0);
}


parse_osmium_t::parse_osmium_t(bool extra_attrs,
                               const boost::optional<std::string> &bbox,
                               const reprojection *proj, bool do_append,
                               osmdata_t *osmdata)
: m_data(osmdata), m_append(do_append), m_attributes(extra_attrs), m_proj(proj)
{
    if (bbox) {
        m_bbox = parse_bbox(bbox);
    }
}

osmium::Box parse_osmium_t::parse_bbox(const boost::optional<std::string> &bbox)
{
    double minx, maxx, miny, maxy;
    int n = sscanf(bbox->c_str(), "%lf,%lf,%lf,%lf",
                   &minx, &miny, &maxx, &maxy);
    if (n != 4)
        throw std::runtime_error("Bounding box must be specified like: minlon,minlat,maxlon,maxlat\n");

    if (maxx <= minx)
        throw std::runtime_error("Bounding box failed due to maxlon <= minlon\n");

    if (maxy <= miny)
        throw std::runtime_error("Bounding box failed due to maxlat <= minlat\n");

    fprintf(stderr, "Applying Bounding box: %f,%f to %f,%f\n", minx, miny, maxx, maxy);

    return osmium::Box(minx, miny, maxx, maxy);
}

void parse_osmium_t::stream_file(const std::string &filename, const std::string &fmt)
{
    const char* osmium_format = fmt == "auto" ? "" : fmt.c_str();
    osmium::io::File infile(filename, osmium_format);

    if (infile.format() == osmium::io::file_format::unknown)
        throw std::runtime_error(fmt.empty()
                                   ?"Cannot detect file format. Try using -r."
                                   : ((boost::format("Unknown file format '%1%'.")
                                                    % fmt).str()));

    fprintf(stderr, "Using %s parser.\n", osmium::io::as_string(infile.format()));

    osmium::io::Reader reader(infile);
    osmium::apply(reader, *this);
    reader.close();
}

void parse_osmium_t::node(osmium::Node& node)
{
    if (node.deleted()) {
        m_data->node_delete(node.id());
    } else {
        // if the node is not valid, then node.location.lat/lon() can throw.
        // we probably ought to treat invalid locations as if they were
        // deleted and ignore them.
        if (!node.location().valid()) {
          fprintf(stderr, "WARNING: Node %" PRIdOSMID " (version %ud) has an invalid "
                  "location and has been ignored. This is not expected to happen with "
                  "recent planet files, so please check that your input is correct.\n",
                  node.id(), node.version());

          return;
        }

        if (!m_bbox || m_bbox->contains(node.location())) {
            auto c = m_proj->reproject(node.location());

            convert_tags(node);
            if (m_append) {
                m_data->node_modify(node.id(), c.y, c.x, tags);
            } else {
                m_data->node_add(node.id(), c.y, c.x, tags);
            }
            m_stats.add_node(node.id());
        }
    }
}

void parse_osmium_t::way(osmium::Way& way)
{
    if (way.deleted()) {
        m_data->way_delete(way.id());
    } else {
        convert_tags(way);
        convert_nodes(way.nodes());
        if (m_append) {
            m_data->way_modify(way.id(), nds, tags);
        } else {
            m_data->way_add(way.id(), nds, tags);
        }
    }
    m_stats.add_way(way.id());
}

void parse_osmium_t::relation(osmium::Relation& rel)
{
    if (rel.deleted()) {
        m_data->relation_delete(rel.id());
    } else {
        if (rel.members().size() > 32767) {
            return;
        }
        convert_tags(rel);
        convert_members(rel.members());
        if (m_append) {
            m_data->relation_modify(rel.id(), members, tags);
        } else {
            m_data->relation_add(rel.id(), members, tags);
        }
    }
    m_stats.add_rel(rel.id());
}

void parse_osmium_t::convert_tags(const osmium::OSMObject &obj)
{
    tags.clear();
    for (auto const &t : obj.tags()) {
        tags.emplace_back(t.key(), t.value());
    }
    if (m_attributes) {
        tags.emplace_back("osm_user", obj.user());
        tags.emplace_back("osm_uid", std::to_string(obj.uid()));
        tags.emplace_back("osm_version", std::to_string(obj.version()));
        tags.emplace_back("osm_timestamp", obj.timestamp().to_iso());
        tags.emplace_back("osm_changeset", std::to_string(obj.changeset()));
    }
}

void parse_osmium_t::convert_nodes(const osmium::NodeRefList &in_nodes)
{
    nds.clear();

    for (auto const &n : in_nodes) {
        nds.push_back(n.ref());
    }
}

void parse_osmium_t::convert_members(const osmium::RelationMemberList &in_rels)
{
    members.clear();

    for (auto const &m: in_rels) {
        OsmType type;
        switch (m.type()) {
            case osmium::item_type::node: type = OSMTYPE_NODE; break;
            case osmium::item_type::way: type = OSMTYPE_WAY; break;
            case osmium::item_type::relation: type = OSMTYPE_RELATION; break;
            default:
                fprintf(stderr, "Unsupported type: %u""\n", unsigned(m.type()));
        }
        members.emplace_back(type, m.ref(), m.role());
    }
}
