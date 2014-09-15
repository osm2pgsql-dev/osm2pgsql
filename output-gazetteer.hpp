#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include "output.hpp"
#include "geometry-builder.hpp"
#include "reprojection.hpp"

#include <boost/shared_ptr.hpp>

class output_gazetteer_t : public output_t {
public:
    output_gazetteer_t(const middle_query_t* mid_, const options_t &options_);
    output_gazetteer_t(const output_gazetteer_t& other);
    virtual ~output_gazetteer_t();

    virtual boost::shared_ptr<output_t> clone();

    int start();
    middle_t::way_cb_func *way_callback();
    middle_t::rel_cb_func *relation_callback();
    void stop();
    void commit();

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
    static const size_t BUFFER_SIZE = 4096;

    void require_slim_mode(void);
    void copy_data(const char *sql);
    void stop_copy(void);
    void delete_unused_classes(char osm_type, osmid_t osm_id, struct keyval *places);
    void add_place(char osm_type, osmid_t osm_id, const char *key_class, const char *type,
                   struct keyval *names, struct keyval *extratags, int adminlevel, 
                   struct keyval *housenumber, struct keyval *street, struct keyval *addr_place, 
                   const char *isin, struct keyval *postcode, struct keyval *countrycode,
                   const char *wkt);
    void delete_place(char osm_type, osmid_t osm_id);
    int gazetteer_process_node(osmid_t id, double lat, double lon, struct keyval *tags,
                               int delete_old);
    int gazetteer_process_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags,
                              int delete_old);
    int gazetteer_process_relation(osmid_t id, struct member *members, int member_count,
                                   struct keyval *tags, int delete_old);
    int connect();

    struct pg_conn *Connection;
    struct pg_conn *ConnectionDelete;
    struct pg_conn *ConnectionError;
    
    int CopyActive;
    unsigned int BufferLen;
    
    char Buffer[BUFFER_SIZE];

    geometry_builder builder;

    boost::shared_ptr<reprojection> reproj;
};

extern output_gazetteer_t out_gazetteer;

#endif
