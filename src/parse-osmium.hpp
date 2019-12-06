#ifndef OSM2PGSQL_PARSE_OSMIUM_HPP
#define OSM2PGSQL_PARSE_OSMIUM_HPP

#include <boost/optional.hpp>
#include <ctime>

#include "osmtypes.hpp"

#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm/box.hpp>

class osmdata_t;

class parse_stats_t
{
    struct Counter
    {
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

        Counter &operator+=(const Counter &rhs)
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

class parse_osmium_t : public osmium::handler::Handler
{
public:
    parse_osmium_t(const boost::optional<std::string> &bbox, bool do_append,
                   osmdata_t *osmdata);

    void stream_file(const std::string &filename, const std::string &fmt);

    void node(osmium::Node const &node);
    void way(osmium::Way &way);
    void relation(osmium::Relation const &rel);

    parse_stats_t const &stats() const { return m_stats; }

private:
    osmium::Box parse_bbox(const boost::optional<std::string> &bbox);

    osmdata_t *m_data;
    bool m_append;
    boost::optional<osmium::Box> m_bbox;
    parse_stats_t m_stats;
    // Current type being parsed.
    osmium::item_type m_type;
};

#endif // OSM2PGSQL_PARSE_OSMIUM_HPP
