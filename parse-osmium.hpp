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

#ifndef PARSE_OSMIUM_H
#define PARSE_OSMIUM_H

#include "config.h"
#include <osmium/handler.hpp>

#include "parse.hpp"

struct reprojection;
class osmdata_t;

namespace osmium {
    class TagList;
    class NodeRefList;
    class RelationMemberList;
}

class parse_osmium_t: public parse_t, public osmium::handler::Handler
{
public:
    parse_osmium_t(const std::string &fmt, int extra_attrs,
                   const bbox_t &bbox, const reprojection *proj);
    void stream_file(const std::string &filename, osmdata_t *osmdata) override;

    void node(osmium::Node& node);
    void way(osmium::Way& way);
    void relation(osmium::Relation& rel);
private:
    void convert_tags(const osmium::OSMObject &obj);
    void convert_nodes(const osmium::NodeRefList &in_nodes);
    void convert_members(const osmium::RelationMemberList &in_rels);
    osmdata_t *data;
    std::string format;
};

#endif
