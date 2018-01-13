#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <osmium/memory/buffer.hpp>

#include "osmium-builder.hpp"
#include "osmtypes.hpp"
#include "output.hpp"
#include "pgsql.hpp"
#include "util.hpp"

/**
 * A private class to convert tags.
 */
class place_tag_processor
{
public:
    place_tag_processor()
        : single_fmt("%1%\t")
    {
        places.reserve(4);
        extratags.reserve(15);
        address.reserve(10);
    }

    ~place_tag_processor() {}

    void process_tags(osmium::OSMObject const &o);

    bool has_data() const { return !places.empty(); }

    bool has_place(const std::string &cls)
    {
        for (const auto& item: places) {
            if (cls == item.key)
                return true;
        }

        return false;
    }

    void copy_out(osmium::OSMObject const &o, const std::string &geom,
                  std::string &buffer);

    void clear();

private:
    void copy_opt_string(char const *val, std::string &buffer)
    {
        if (val) {
            escape(val, buffer);
            buffer += "\t";
        } else {
            buffer += "\\N\t";
        }
    }

    std::string domain_name(const std::string &cls, osmium::TagList const &tags)
    {
        std::string ret;
        bool hasname = false;

        std::string prefix(cls + ":name");

        for (const auto& item: tags) {
            char const *k = item.key();
            if (boost::starts_with(k, prefix)
                && (k[prefix.length()] == '\0' || k[prefix.length()] == ':')) {
                if (!hasname) {
                    hasname = true;
                } else {
                    ret += ",";
                }
                ret += "\"";
                escape_array_record(k + cls.length() + 1, ret);
                ret += "\"=>\"";
                escape_array_record(item.value(), ret);
                ret += "\"";
            }
        }

        return ret;
    }


    void escape_array_record(const std::string &in, std::string &out)
    {
        for (const char c: in) {
            switch(c) {
                case '\\':
                    // Tripple escaping required: string escaping leaves us
                    // with 4 backslashes, COPY then reduces it to two, which
                    // are then interpreted as a single backslash by the hash
                    // parsing code.
                    out += "\\\\\\\\";
                    break;
                case '\n':
                case '\r':
                case '\t':
                case '"':
                    /* This is a bit naughty - we know that nominatim ignored these characters so just drop them now for simplicity */
                           out += ' '; break;
                default:   out += c; break;
            }
        }
    }


    std::vector<tag_t> places;
    std::vector<osmium::Tag const *> names;
    std::vector<osmium::Tag const *> extratags;
    std::unordered_map<std::string, char const *> address;
    int admin_level;

    boost::format single_fmt;
};


class output_gazetteer_t : public output_t {
public:
    output_gazetteer_t(const middle_query_t *mid_, const options_t &options_)
    : output_t(mid_, options_), Connection(NULL), ConnectionDelete(NULL),
      ConnectionError(NULL), copy_active(false),
      m_builder(options_.projection, true), single_fmt("%1%"),
      osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {
        buffer.reserve(PLACE_BUFFER_SIZE);
    }

    output_gazetteer_t(const output_gazetteer_t &other)
    : output_t(other.m_mid, other.m_options), Connection(NULL),
      ConnectionDelete(NULL), ConnectionError(NULL), copy_active(false),
      m_builder(other.m_options.projection, true), single_fmt(other.single_fmt),
      osmium_buffer(PLACE_BUFFER_SIZE, osmium::memory::Buffer::auto_grow::yes)
    {
        buffer.reserve(PLACE_BUFFER_SIZE);
        connect();
    }

    virtual ~output_gazetteer_t() {}

    std::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const override
    {
        output_gazetteer_t *clone = new output_gazetteer_t(*this);
        clone->m_mid = cloned_middle;
        return std::shared_ptr<output_t>(clone);
    }

    int start() override;
    void stop(osmium::thread::Pool *pool) override;
    void commit() override {}

    void enqueue_ways(pending_queue_t &, osmid_t, size_t, size_t&) override {}
    int pending_way(osmid_t, int) override { return 0; }

    void enqueue_relations(pending_queue_t &, osmid_t, size_t, size_t&) override {}
    int pending_relation(osmid_t, int) override { return 0; }

    int node_add(osmium::Node const &node) override
    {
        return process_node(node);
    }

    int way_add(osmium::Way *way) override
    {
        return process_way(way);
    }

    int relation_add(osmium::Relation const &rel) override
    {
        return process_relation(rel);
    }

    int node_modify(osmium::Node const &node) override
    {
        return process_node(node);
    }

    int way_modify(osmium::Way *way) override
    {
        return process_way(way);
    }

    int relation_modify(osmium::Relation const &rel) override
    {
        return process_relation(rel);
    }

    int node_delete(osmid_t id) override
    {
        delete_place('N', id);
        return 0;
    }

    int way_delete(osmid_t id) override
    {
        delete_place('W', id);
        return 0;
    }

    int relation_delete(osmid_t id) override
    {
        delete_place('R', id);
        return 0;
    }

private:
    enum { PLACE_BUFFER_SIZE = 4096 };

    void stop_copy(void);
    void delete_unused_classes(char osm_type, osmid_t osm_id);
    void delete_place(char osm_type, osmid_t osm_id);
    int process_node(osmium::Node const &node);
    int process_way(osmium::Way *way);
    int process_relation(osmium::Relation const &rel);
    int connect();

    void flush_place_buffer()
    {
        if (!copy_active)
        {
            pgsql_exec(Connection, PGRES_COPY_IN,
                       "COPY place (osm_type, osm_id, class, type, name, "
                       "admin_level, address, extratags, geometry) FROM STDIN");
            copy_active = true;
        }

        pgsql_CopyData("place", Connection, buffer);
        buffer.clear();
    }

    void delete_unused_full(char osm_type, osmid_t osm_id)
    {
        if (m_options.append) {
            places.clear();
            delete_place(osm_type, osm_id);
        }
    }

    struct pg_conn *Connection;
    struct pg_conn *ConnectionDelete;
    struct pg_conn *ConnectionError;

    bool copy_active;

    std::string buffer;
    place_tag_processor places;

    geom::osmium_builder_t m_builder;

    // string formatters
    // Need to be part of the class, so we have one per thread.
    boost::format single_fmt;
    osmium::memory::Buffer osmium_buffer;
};

#endif
