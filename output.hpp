/* Common output layer interface */

/* Each output layer must provide methods for 
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc. 
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "middle.hpp"
#include "options.hpp"

#include <boost/noncopyable.hpp>

class output_t : public boost::noncopyable {
public:
    output_t(middle_t* mid_, const options_t* options_);
    virtual ~output_t();

    virtual int start() = 0;
    virtual int connect(int startTransaction) = 0;
    virtual void stop() = 0;
    virtual void cleanup(void) = 0;
    virtual void close(int stopTransaction) = 0;

    virtual int node_add(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_modify(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_delete(osmid_t id) = 0;
    virtual int way_delete(osmid_t id) = 0;
    virtual int relation_delete(osmid_t id) = 0;

    virtual const options_t* get_options()const;

protected:
    output_t();
    middle_t* m_mid;
    const options_t* m_options;
};

unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon);

#endif
