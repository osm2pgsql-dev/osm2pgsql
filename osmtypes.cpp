#include "osmtypes.hpp"
#include "middle.hpp"
#include "output.hpp"

#include <boost/foreach.hpp>
#include <stdexcept>

osmdata_t::osmdata_t(middle_t* mid_, output_t* out_): mid(mid_)
{
    outs.push_back(out_);
}

osmdata_t::osmdata_t(middle_t* mid_, const std::vector<output_t*> &outs_)
    : mid(mid_), outs(outs_)
{
    if (outs.empty()) {
        throw std::runtime_error("Must have at least one output, but none have "
                                 "been configured.");
    }
}

osmdata_t::~osmdata_t()
{
}

int osmdata_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags) {
    mid->nodes_set(id, lat, lon, tags);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->node_add(id, lat, lon, tags);
    }
    return status;
}

int osmdata_t::way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    mid->ways_set(id, nodes, node_count, tags);
    
    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->way_add(id, nodes, node_count, tags);
    }
    return status;
}

int osmdata_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    mid->relations_set(id, members, member_count, tags);
    
    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->relation_add(id, members, member_count, tags);
    }
    return status;
}

int osmdata_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->nodes_delete(id);
    slim->nodes_set(id, lat, lon, tags);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->node_modify(id, lat, lon, tags);
    }

    slim->node_changed(id);

    return status;
}

int osmdata_t::way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->ways_delete(id);
    slim->ways_set(id, nodes, node_count, tags);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->way_modify(id, nodes, node_count, tags);
    }

    slim->way_changed(id);

    return status;
}

int osmdata_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    slim->relations_delete(id);
    slim->relations_set(id, members, member_count, tags);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->relation_modify(id, members, member_count, tags);
    }

    slim->relation_changed(id);

    return status;
}

int osmdata_t::node_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->node_delete(id);
    }

    slim->nodes_delete(id);

    return status;
}

int osmdata_t::way_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->way_delete(id);
    }

    slim->ways_delete(id);

    return status;
}

int osmdata_t::relation_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

    int status = 0;
    BOOST_FOREACH(output_t *out, outs) {
        status |= out->relation_delete(id);
    }

    slim->relations_delete(id);

    return status;
}

void osmdata_t::start() {
    BOOST_FOREACH(output_t *out, outs) {
        out->start();
    }
    mid->start(outs[0]->get_options());
}

namespace {

struct way_cb_func : public middle_t::way_cb_func {
    way_cb_func() {}
    void add(middle_t::way_cb_func *ptr) { m_ptrs.push_back(ptr); }
    bool empty() const { return m_ptrs.empty(); }
    virtual ~way_cb_func() {
        BOOST_FOREACH(middle_t::way_cb_func *ptr, m_ptrs) {
            delete ptr;
        }
    }
    int operator()(osmid_t id, struct keyval *tags, const struct osmNode *nodes, int count, int exists) {
        int status = 0;
        BOOST_FOREACH(middle_t::way_cb_func *ptr, m_ptrs) {
            status |= ptr->operator()(id, tags, nodes, count, exists);
        }
        return status;
    }
    void finish(int exists) { 
        BOOST_FOREACH(middle_t::way_cb_func *ptr, m_ptrs) {
            ptr->finish(exists);
        }
    }
    std::vector<middle_t::way_cb_func*> m_ptrs;
};

struct rel_cb_func : public middle_t::rel_cb_func {
    rel_cb_func() {}
    void add(middle_t::rel_cb_func *ptr) { m_ptrs.push_back(ptr); }
    bool empty() const { return m_ptrs.empty(); }
    virtual ~rel_cb_func() {
        BOOST_FOREACH(middle_t::rel_cb_func *ptr, m_ptrs) {
            delete ptr;
        }
    }
    int operator()(osmid_t id, const struct member *members, int member_count, struct keyval *tags, int exists) {
        int status = 0;
        BOOST_FOREACH(middle_t::rel_cb_func *ptr, m_ptrs) {
            status |= ptr->operator()(id, members, member_count, tags, exists);
        }
        return status;
    }
    void finish(int exists) { 
        BOOST_FOREACH(middle_t::rel_cb_func *ptr, m_ptrs) {
            ptr->finish(exists);
        }
    }
    std::vector<middle_t::rel_cb_func*> m_ptrs;
};

} // anonymous namespace

void osmdata_t::stop() {
    /* Commit the transactions, so that multiple processes can
     * access the data simultanious to process the rest in parallel
     * as well as see the newly created tables.
     */
    mid->commit();
    BOOST_FOREACH(output_t *out, outs) {
        out->commit();
    }

    // should be the same for all outputs
    const int append = outs[0]->get_options()->append;

	/* Pending ways
     * This stage takes ways which were processed earlier, but might be
     * involved in a multipolygon relation. They could also be ways that
     * were modified in diff processing.
     */
    {
        way_cb_func callback;
        BOOST_FOREACH(output_t *out, outs) {
            middle_t::way_cb_func *way_callback = out->way_callback();
            if (way_callback != NULL) {
                callback.add(way_callback);
            }
        }
        if (!callback.empty()) {
            mid->iterate_ways( callback );
            callback.finish(append);

            mid->commit();
            BOOST_FOREACH(output_t *out, outs) {
                out->commit();
            }
        }
    }

	/* Pending relations
	 * This is like pending ways, except there aren't pending relations
	 * on import, only on update.
	 * TODO: Can we skip this on import?
	 */
    {
        rel_cb_func callback;
        BOOST_FOREACH(output_t *out, outs) {
            middle_t::rel_cb_func *rel_callback = out->relation_callback();
            if (rel_callback != NULL) {
                callback.add(rel_callback);
            }
        }
        if (!callback.empty()) {
            mid->iterate_relations( callback );
            callback.finish(append);

            mid->commit();
            BOOST_FOREACH(output_t *out, outs) {
                out->commit();
            }
        }
    }

	/* Clustering, index creation, and cleanup.
	 * All the intensive parts of this are long-running PostgreSQL commands
	 */
    mid->stop();
    BOOST_FOREACH(output_t *out, outs) {
        out->stop();
    }
}
