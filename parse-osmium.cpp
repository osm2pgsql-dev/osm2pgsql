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


parse_osmium_t::parse_osmium_t(const std::string &fmt, int extra_attrs,
                               const bbox_t &bbox, const reprojection *proj,
                               bool do_append)
: parse_t(extra_attrs, bbox, proj), data(nullptr),
  format(fmt == "auto"?"":fmt), append(do_append)
{}

void
parse_osmium_t::stream_file(const std::string &filename, osmdata_t *osmdata)
{
    data = osmdata;
    osmium::io::File infile(filename, format);

    if (infile.format() == osmium::io::file_format::unknown)
        throw std::runtime_error(format.empty()
                                   ?"Cannot detect file format. Try using -r."
                                   : ((boost::format("Unknown file format '%1%'.")
                                                    % format).str()));

    fprintf(stderr, "Using %s parser.\n", osmium::io::as_string(infile.format()));

    osmium::io::Reader reader(infile);
    osmium::apply(reader, *this);
    reader.close();
    data = nullptr;
}

void parse_osmium_t::node(osmium::Node& node)
{
    // if the node is not valid, then node.location.lat/lon() can throw.
    // we probably ought to treat invalid locations as if they were
    // deleted and ignore them.
    if (!node.location().valid()) { return; }

    double lat = node.location().lat_without_check();
    double lon = node.location().lon_without_check();
    if (bbox.inside(lat, lon)) {
        proj->reproject(&lat, &lon);

        if (node.deleted()) {
            data->node_delete(node.id());
        } else {
            convert_tags(node);
            if (append) {
                data->node_modify(node.id(), lat, lon, tags);
            } else {
                data->node_add(node.id(), lat, lon, tags);
            }
        }

        stats.add_node(node.id());
    }
}

void parse_osmium_t::way(osmium::Way& way)
{
    if (way.deleted()) {
        data->way_delete(way.id());
    } else {
        convert_tags(way);
        convert_nodes(way.nodes());
        if (append) {
            data->way_modify(way.id(), nds, tags);
        } else {
            data->way_add(way.id(), nds, tags);
        }
    }
    stats.add_way(way.id());
}

void parse_osmium_t::relation(osmium::Relation& rel)
{
    if (rel.deleted()) {
        data->relation_delete(rel.id());
    } else {
        convert_tags(rel);
        convert_members(rel.members());
        if (append) {
            data->relation_modify(rel.id(), members, tags);
        } else {
            data->relation_add(rel.id(), members, tags);
        }
    }
    stats.add_rel(rel.id());
}

void parse_osmium_t::convert_tags(const osmium::OSMObject &obj)
{
    tags.clear();
    for (auto const &t : obj.tags()) {
        tags.emplace_back(t.key(), t.value());
    }
    if (extra_attributes) {
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
