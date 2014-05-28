
#include "osmtypes.hpp"
#include "middle.hpp"
#include "output.hpp"


osmdata_t::osmdata_t(middle_t* mid_, output_t* out_): mid(mid_), out(out_)
{
}

osmdata_t::~osmdata_t()
{
}

int osmdata_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags) {
    mid->nodes_set(id, lat, lon, tags);
    return out->node_add(id, lat, lon, tags);
}

int osmdata_t::way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    mid->ways_set(id, nodes, node_count, tags);
    return out->way_add(id, nodes, node_count, tags);
}

int osmdata_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    mid->relations_set(id, members, member_count, tags);
    return out-> relation_add(id, members, member_count, tags);
}

int osmdata_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->nodes_delete(id);
    slim->nodes_set(id, lat, lon, tags);

    int status = out->node_modify(id, lat, lon, tags);

    slim->node_changed(id);

    return status;
}

int osmdata_t::way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->ways_delete(id);
    slim->ways_set(id, nodes, node_count, tags);

    int status = out->way_modify(id, nodes, node_count, tags);

    slim->way_changed(id);

    return status;
}

int osmdata_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->relations_delete(id);
    slim->relations_set(id, members, member_count, tags);

    int status = out->relation_modify(id, members, member_count, tags);

    slim->relation_changed(id);

    return status;
}

int osmdata_t::node_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = out->node_delete(id);

    slim->nodes_delete(id);

    return status;
}

int osmdata_t::way_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = out->way_delete(id);

    slim->ways_delete(id);

    return status;
}

int osmdata_t::relation_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = out->relation_delete(id);

    slim->relations_delete(id);

    return status;
}

void osmdata_t::start() {
    out->start();
    mid->start(out->get_options());
}

namespace {

struct way_cb_func : public middle_t::way_cb_func {
    way_cb_func(middle_t::way_cb_func *ptr) : m_ptr(ptr) { }
    virtual ~way_cb_func() { }
    int operator()(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists) {
        return m_ptr->operator()(id, tags, nodes, count, exists);
    }
    void finish(int exists) { m_ptr->finish(exists); }
    middle_t::way_cb_func *m_ptr;
};

struct rel_cb_func : public middle_t::rel_cb_func  {
    rel_cb_func(middle_t::rel_cb_func *ptr) : m_ptr(ptr) { }
    virtual ~rel_cb_func() { }
    int operator()(osmid_t id, struct member *members, int member_count, struct keyval *tags, int exists) {
        return m_ptr->operator()(id, members, member_count, tags, exists);
    }
    void finish(int exists) { m_ptr->finish(exists); }
    middle_t::rel_cb_func *m_ptr;
};

} // anonymous namespace

void osmdata_t::stop() {
    /* Commit the transactions, so that multiple processes can
     * access the data simultanious to process the rest in parallel
     * as well as see the newly created tables.
     */
    mid->commit();
    out->commit();

    middle_t::way_cb_func *way_callback = out->way_callback();
    if (way_callback != NULL) {
        way_cb_func callback(way_callback);
        mid->iterate_ways( callback );
        callback.finish(out->get_options()->append);

        mid->commit();
        out->commit();
    }

    middle_t::rel_cb_func *rel_callback = out->relation_callback();
    if (rel_callback != NULL) {
        rel_cb_func callback(rel_callback);
        mid->iterate_relations( callback );
        callback.finish(out->get_options()->append);

        mid->commit();
        out->commit();
    }

    mid->stop();
    out->stop();
}

void osmdata_t::cleanup() {
    out->cleanup();
}
