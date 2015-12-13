#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include <memory>
#include <string>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "geometry-builder.hpp"
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

    void process_tags(const taglist_t &tags);

    bool has_data() const { return !places.empty(); }

    bool has_place(const std::string &cls)
    {
        for (const auto& item: places) {
            if (cls == item.key)
                return true;
        }

        return false;
    }

    void copy_out(char osm_type, osmid_t osm_id, const std::string &geom,
                  std::string &buffer);

    void clear();

private:
    void copy_opt_string(const std::string *val, std::string &buffer)
    {
        if (val) {
            escape(*val, buffer);
            buffer += "\t";
        } else {
            buffer += "\\N\t";
        }
    }

    std::string domain_name(const std::string &cls)
    {
        std::string ret;
        bool hasname = false;

        std::string prefix(cls + ":name");

        for (const auto& item: *src) {
            if (boost::starts_with(item.key, prefix) &&
                (item.key.length() == prefix.length()
                 || item.key[prefix.length()] == ':')) {
                if (!hasname) {
                    ret.reserve(item.key.length() + item.value.length() + 10);
                    hasname = true;
                } else
                    ret += ",";
                ret += "\"";
                escape_array_record(std::string(item.key, cls.length() + 1), ret);
                ret += "\"=>\"";
                escape_array_record(item.value, ret);
                ret += "\"";
            }
        }

        return ret;
    }


    void escape_array_record(const std::string &in, std::string &out)
    {
        for (const char c: in) {
            switch(c) {
                case '\\': out += "\\\\\\\\\\\\\\\\"; break;
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
    std::vector<const tag_t *> names;
    std::vector<const tag_t *> extratags;
    std::vector<const tag_t *> address;
    const taglist_t *src;
    int admin_level;
    const std::string *countrycode;
    std::string housenumber;
    const std::string *street;
    const std::string *addr_place;
    const std::string *postcode;

    boost::format single_fmt;
public:
    std::string srid_str;
};


class output_gazetteer_t : public output_t {
public:
    output_gazetteer_t(const middle_query_t* mid_, const options_t &options_)
    : output_t(mid_, options_),
      Connection(NULL),
      ConnectionDelete(NULL),
      ConnectionError(NULL),
      copy_active(false),
      single_fmt("%1%"),
      point_fmt("POINT(%.15g %.15g)")
    {
        buffer.reserve(PLACE_BUFFER_SIZE);
    }

    output_gazetteer_t(const output_gazetteer_t& other)
    : output_t(other.m_mid, other.m_options),
      Connection(NULL),
      ConnectionDelete(NULL),
      ConnectionError(NULL),
      copy_active(false),
      reproj(other.reproj),
      single_fmt(other.single_fmt),
      point_fmt(other.point_fmt)
    {
        buffer.reserve(PLACE_BUFFER_SIZE);
        builder.set_exclude_broken_polygon(m_options.excludepoly);
        connect();
    }

    virtual ~output_gazetteer_t() {}

    virtual std::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const
    {
        output_gazetteer_t *clone = new output_gazetteer_t(*this);
        clone->m_mid = cloned_middle;
        return std::shared_ptr<output_t>(clone);
    }

    int start();
    void stop();
    void commit() {}

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {}
    int pending_way(osmid_t id, int exists) { return 0; }

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {}
    int pending_relation(osmid_t id, int exists) { return 0; }

    int node_add(osmid_t id, double lat, double lon, const taglist_t &tags)
    {
        return process_node(id, lat, lon, tags);
    }

    int way_add(osmid_t id, const idlist_t &nodes, const taglist_t &tags)
    {
        return process_way(id, nodes, tags);
    }

    int relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags)
    {
        return process_relation(id, members, tags);
    }

    int node_modify(osmid_t id, double lat, double lon, const taglist_t &tags)
    {
        return process_node(id, lat, lon, tags);
    }

    int way_modify(osmid_t id, const idlist_t &nodes, const taglist_t &tags)
    {
        return process_way(id, nodes, tags);
    }

    int relation_modify(osmid_t id, const memberlist_t &members, const taglist_t &tags)
    {
        return process_relation(id, members, tags);
    }

    int node_delete(osmid_t id)
    {
        delete_place('N', id);
        return 0;
    }

    int way_delete(osmid_t id)
    {
        delete_place('W', id);
        return 0;
    }

    int relation_delete(osmid_t id)
    {
        delete_place('R', id);
        return 0;
    }

private:
    enum { PLACE_BUFFER_SIZE = 4092 };

    void stop_copy(void);
    void delete_unused_classes(char osm_type, osmid_t osm_id);
    void delete_place(char osm_type, osmid_t osm_id);
    int process_node(osmid_t id, double lat, double lon, const taglist_t &tags);
    int process_way(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int process_relation(osmid_t id, const memberlist_t &members, const taglist_t &tags);
    int connect();

    void flush_place_buffer()
    {
        if (!copy_active)
        {
            pgsql_exec(Connection, PGRES_COPY_IN, "COPY place (osm_type, osm_id, class, type, name, admin_level, housenumber, street, addr_place, isin, postcode, country_code, extratags, geometry) FROM STDIN");
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

    geometry_builder builder;

    std::shared_ptr<reprojection> reproj;

    // string formatters
    // Need to be part of the class, so we have one per thread.
    boost::format single_fmt;
    boost::format point_fmt;

    const static std::string NAME;
};

extern output_gazetteer_t out_gazetteer;

#endif
