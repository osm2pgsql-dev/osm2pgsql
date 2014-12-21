#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include "output.hpp"
#include "geometry-builder.hpp"
#include "reprojection.hpp"
#include "util.hpp"

#include <boost/shared_ptr.hpp>

#include <string>
#include <iostream>

class output_gazetteer_t : public output_t {
public:
    output_gazetteer_t(const middle_query_t* mid_, const options_t &options_);
    output_gazetteer_t(const output_gazetteer_t& other);
    virtual ~output_gazetteer_t() {}

    virtual boost::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const;

    int start();
    void stop();
    void commit();

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_way(osmid_t id, int exists);

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_relation(osmid_t id, int exists);

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

private:
    enum { PLACE_BUFFER_SIZE = 4092 };

    void stop_copy(void);
    void delete_unused_classes(char osm_type, osmid_t osm_id, struct keyval *places);
    void add_place(char osm_type, osmid_t osm_id, const std::string &key_class, const std::string &type,
                   struct keyval *names, struct keyval *extratags, int adminlevel,
                   struct keyval *housenumber, struct keyval *street, struct keyval *addr_place,
                   const char *isin, struct keyval *postcode, struct keyval *countrycode,
                   const std::string &wkt);
    void delete_place(char osm_type, osmid_t osm_id);
    int gazetteer_process_node(osmid_t id, double lat, double lon, struct keyval *tags,
                               int delete_old);
    int gazetteer_process_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags,
                              int delete_old);
    int gazetteer_process_relation(osmid_t id, struct member *members, int member_count,
                                   struct keyval *tags, int delete_old);
    int connect();

    void require_slim_mode(void) {
        if (!m_options.slim)
        {
            std::cerr << "Cannot apply diffs unless in slim mode\n";
            util::exit_nicely();
        }
    }

    void flush_place_buffer()
    {
        if (buffer.length() >= PLACE_BUFFER_SIZE - 10
            || (buffer.length() > 0 && buffer[buffer.length() - 1] == '\n'))
        {
            if (!CopyActive)
            {
                pgsql_exec(Connection, PGRES_COPY_IN, "COPY place (osm_type, osm_id, class, type, name, admin_level, housenumber, street, addr_place, isin, postcode, country_code, extratags, geometry) FROM STDIN");
                CopyActive = 1;
            }

            pgsql_CopyData("place", Connection, buffer);
            buffer.clear();
        }
    }

    void value_to_place_table(struct keyval* kv)
    {
        if (kv)
        {
            escape(kv->value, buffer);
            buffer += "\t";
            flush_place_buffer();
        }
        else
            buffer += "\\N\t";
    }

    void hstore_to_place_table(struct keyval *values)
    {
        if (keyval::listHasData(values))
        {
            bool first = true;
            for (struct keyval *entry = keyval::firstItem(values); 
                 entry;
                 entry = keyval::nextItem(values, entry))
            {
                if (first) first = false;
                else buffer += ',';

                buffer += "\"";

                escape_array_record(entry->key, buffer);
                flush_place_buffer();

                buffer += "\"=>\"";

                escape_array_record(entry->value, buffer);
                flush_place_buffer();

                buffer += "\"";
            }
            buffer += "\t";
        }
        else
            buffer += "\\N\t";
    }

    void escape_array_record(const char *in, std::string &out)
    {
        while(*in) {
            switch(*in) {
                case '\\': out += "\\\\\\\\\\\\\\\\"; break;
                case '\n':
                case '\r':
                case '\t':
                case '"':
                    /* This is a bit naughty - we know that nominatim ignored these characters so just drop them now for simplicity */
                           out += ' '; break;
                default:   out += *in; break;
            }
            in++;
        }
    }

    struct pg_conn *Connection;
    struct pg_conn *ConnectionDelete;
    struct pg_conn *ConnectionError;

    int CopyActive;

    std::string buffer;

    geometry_builder builder;

    boost::shared_ptr<reprojection> reproj;

    // string formatters
    // Need to be part of the class, so we have one per thread.
    boost::format single_fmt;
    boost::format point_fmt;

    const static std::string NAME;
};

extern output_gazetteer_t out_gazetteer;

#endif
