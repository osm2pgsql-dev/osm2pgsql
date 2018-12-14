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

#include <boost/optional.hpp>
#include <ctime>

#include "osmtypes.hpp"

#include <osmium/osm/box.hpp>
#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>


class osmdata_t;

class parse_stats_t
{
    struct Counter {
        osmid_t count = 0;
        osmid_t max = 0;
        time_t start = 0;

        bool add(osmid_t id, int frac)
        {
            if (id > max) {
                max = id;
            }
            if (count == 0) {
                time(&start);
            }
            count++;

            return (count % frac == 0);
        }

        Counter& operator+=(const Counter& rhs)
        {
            count += rhs.count;
            if (rhs.max > max) {
                max = rhs.max;
            }
            if (start == 0) {
                start = rhs.start;
            }

            return *this;
        }
    };

public:
    parse_stats_t() : print_time(time(nullptr)) {}

    void update(const parse_stats_t &other);
    void print_summary() const;
    void print_status();

    inline void add_node(osmid_t id)
    {
        if (node.add(id, 10000)) {
            print_status();
        }
    }

    inline void add_way(osmid_t id)
    {
        if (way.add(id, 1000)) {
            print_status();
        }
    }

    inline void add_rel(osmid_t id)
    {
        if (rel.add(id, 10)) {
           print_status();
        }
    }

private:
    Counter node, way, rel;
    time_t print_time;
};


class parse_osmium_t: public osmium::handler::Handler
{
public:
    parse_osmium_t(const boost::optional<std::string> &bbox,
                   bool do_append, osmdata_t *osmdata);

    void stream_file(const std::string &filename, const std::string &fmt);

    void node(osmium::Node const &node);
    void way(osmium::Way& way);
    void relation(osmium::Relation const &rel);

    parse_stats_t const &stats() const
    {
        return m_stats;
    }

private:
    osmium::Box parse_bbox(const boost::optional<std::string> &bbox);

    osmdata_t *m_data;
    bool m_append;
    boost::optional<osmium::Box> m_bbox;
    parse_stats_t m_stats;
    // Current type being parsed.
    osmium::item_type m_type;
};

#endif
