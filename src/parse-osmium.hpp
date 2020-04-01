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
        std::time_t start = 0;
        int m_frac;

        Counter(int frac) noexcept : m_frac(frac) {
        }

        osmid_t count_k() const noexcept {
            return count / 1000;
        }

        bool add(osmid_t id) noexcept
        {
            if (id > max) {
                max = id;
            }
            if (count == 0) {
                start = std::time(nullptr);
            }
            ++count;

            return count % m_frac == 0;
        }

        Counter &operator+=(Counter const &rhs) noexcept
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
    parse_stats_t() noexcept : m_last_print_time(std::time(nullptr)) {}

    void update(parse_stats_t const &other);
    void print_summary() const;
    void print_status();

    void add_node(osmid_t id)
    {
        if (m_node.add(id)) {
            print_status();
        }
    }

    void add_way(osmid_t id)
    {
        if (m_way.add(id)) {
            print_status();
        }
    }

    void add_rel(osmid_t id)
    {
        if (m_rel.add(id)) {
            print_status();
        }
    }

private:
    static double count_per_second(osmid_t count, uint64_t elapsed) noexcept
    {
        if (count == 0) {
            return 0.0;
        }

        if (elapsed == 0) {
            return count;
        }

        return static_cast<double>(count) / elapsed;
    }

    uint64_t nodes_time(std::time_t now) const noexcept
    {
        if (m_node.count == 0) {
            return 0;
        }
        return (m_way.start > 0 ? m_way.start : now) - m_node.start;
    }

    uint64_t ways_time(std::time_t now) const noexcept
    {
        if (m_way.count == 0) {
            return 0;
        }
        return (m_rel.start > 0 ? m_rel.start : now) - m_way.start;
    }

    uint64_t rels_time(std::time_t now) const noexcept
    {
        if (m_rel.count == 0) {
            return 0;
        }
        return now - m_rel.start;
    }

    Counter m_node{10000};
    Counter m_way{1000};
    Counter m_rel{10};
    std::time_t m_last_print_time;
};

class parse_osmium_t : public osmium::handler::Handler
{
public:
    parse_osmium_t(boost::optional<std::string> const &bbox, bool do_append,
                   osmdata_t *osmdata);

    void stream_file(std::string const &filename, std::string const &fmt);

    void node(osmium::Node const &node);
    void way(osmium::Way &way);
    void relation(osmium::Relation const &rel);

    parse_stats_t const &stats() const noexcept { return m_stats; }

private:
    osmium::Box parse_bbox(boost::optional<std::string> const &bbox);

    osmdata_t *m_data;
    bool m_append;
    boost::optional<osmium::Box> m_bbox;
    parse_stats_t m_stats;
    // Current type being parsed.
    osmium::item_type m_type;
};

#endif // OSM2PGSQL_PARSE_OSMIUM_HPP
