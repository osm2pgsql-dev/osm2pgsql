#include "osmtypes.hpp"
#include "middle.hpp"
#include "output.hpp"

#include <boost/atomic.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>

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

struct cb_func : public middle_t::cb_func {
    cb_func() {}
    void add(middle_t::cb_func *ptr) { m_ptrs.push_back(ptr); }
    bool empty() const { return m_ptrs.empty(); }
    virtual ~cb_func() {
        BOOST_FOREACH(middle_t::cb_func *ptr, m_ptrs) {
            delete ptr;
        }
    }
    int operator()(osmid_t id, int exists) {
        int status = 0;
        BOOST_FOREACH(middle_t::cb_func *ptr, m_ptrs) {
            status |= ptr->operator()(id, exists);
        }
        return status;
    }
    void finish(int exists) { 
        BOOST_FOREACH(middle_t::cb_func *ptr, m_ptrs) {
            ptr->finish(exists);
        }
    }
    std::vector<middle_t::cb_func*> m_ptrs;
};

struct pending_job {
    //id we need to process
    osmid_t id;

    //whether this is a way or a rel
    bool is_way;

    //TODO: add these to the thread run method
    //copy of the backends table so it can write the stuff back to it
    //copy of the backends geometry processor
    //copy of the tag transform so we can add/change/delete tags on ways/rels
    //copy of the tag export list so we know what the ending columns will be




    //TODO: when processing a way we need to synchronize access to the rels
    //pending (or have those ids as output from completing a batch)

    //TODO: when expiring we need to synchronize access to or make our own in memory
    //expiry list to provide as output

    //------------------------------------

    //TODO: batch ids so we can query more than one at once from the middle

    //TODO: worry about dealloc of tags and nodes and members
    //and what happens when copying this stuff (push and pop on queue)
    //since multiple jobs will consume the same copy of these things we
    //kind of have to do it outside of the queue or make deep copies of them
};

struct pending_processor {

    static void do_batch(boost::lockfree::queue<pending_job>& queue,
                         boost::atomic_size_t& ids_done,
                         const boost::atomic<bool>& done);

    //starts up count threads and works on the queue
    pending_processor(size_t thread_count, size_t job_count);

    //waits for the completion of all outstanding jobs
    void join();

    //actual threads
    boost::thread_group workers;
    //job queue
    boost::lockfree::queue<pending_job> queue;
    //whether or not we are done putting jobs in the queue
    boost::atomic<bool> done;
    //how many ids within the job have been processed
    boost::atomic_size_t ids_done;
};

void pending_processor::do_batch(boost::lockfree::queue<pending_job>& queue,
                                 boost::atomic_size_t& ids_done,
                                 const boost::atomic<bool>& done) {
    pending_job job;

    //if we got something or we arent done putting things in the queue
    while (queue.pop(job) || !done) {
        //TODO: reprocess way/rel

        //finished one
        ++ids_done;
    }

    while (queue.pop(job)) {
        //TODO: reprocess way/rel

        //finished one
        ++ids_done;
    }
}

//starts up count threads and works on the queue
pending_processor::pending_processor(size_t thread_count, size_t job_count) {
    //we are not done adding jobs yet
    done = false;

    //nor have we completed any
    ids_done = 0;

    //reserve space for the jobs
    queue.reserve(job_count);

    //make the threads and start them
    for (size_t i = 0; i != thread_count; ++i) {
        workers.create_thread(
            boost::bind(do_batch, boost::ref(queue), boost::ref(ids_done), boost::cref(done)));
    }
}

//waits for the completion of all outstanding jobs
void pending_processor::join() {
    //we are done adding jobs
    done = true;

    //wait for them to really be done
    workers.join_all();
}

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
        cb_func callback;
        BOOST_FOREACH(output_t *out, outs) {
            middle_t::cb_func *way_callback = out->way_callback();
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
        cb_func callback;
        BOOST_FOREACH(output_t *out, outs) {
            middle_t::cb_func *rel_callback = out->relation_callback();
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
