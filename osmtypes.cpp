#include "osmtypes.hpp"
#include "middle.hpp"
#include "output.hpp"

#include <boost/atomic.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
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

    int exists;

    //whether this is a way or a rel
    bool is_way;

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

    typedef boost::lockfree::queue<pending_job> queue_t;

    static void do_batch(middle_t::cb_func *callback,
                         queue_t& queue,
                         boost::atomic_size_t& ids_done,
                         const boost::atomic<bool>& done,
                         int append);

    //starts up count threads and works on the queue
    pending_processor(output_t *out, size_t thread_count, size_t job_count, int append)
        : outputs(thread_count), queue(job_count), append(append) {

        //we are not done adding jobs yet
        done = false;

        //nor have we completed any
        ids_done = 0;

        //make the threads and start them
        for (size_t i = 0; i != thread_count - 1; ++i) {
            boost::shared_ptr<output_t> clone = out->clone();
            outputs.push_back(clone);
            middle_t::cb_func *callback = clone->way_callback();
            workers.create_thread(boost::bind(do_batch, callback, boost::ref(queue), boost::ref(ids_done), boost::cref(done), append));
        }
    }

    void submit(const pending_job &job) {
        queue.push(job);
    }

    //waits for the completion of all outstanding jobs
    void join() {
        //we are done adding jobs
        done = true;
        //wait for them to really be done
        workers.join_all();
    }

private:
    //output copies
    std::vector<boost::shared_ptr<output_t> > outputs;
    //actual threads
    boost::thread_group workers;
    //job queue
    queue_t queue;
    //whether or not we are done putting jobs in the queue
    boost::atomic<bool> done;
    //how many ids within the job have been processed
    boost::atomic_size_t ids_done;
    int append;
};

void pending_processor::do_batch(middle_t::cb_func *callback,
                                 pending_processor::queue_t& queue,
                                 boost::atomic_size_t& ids_done,
                                 const boost::atomic<bool>& done,
                                 int append) {
    pending_job job;

    //if we got something or we arent done putting things in the queue
    while (queue.pop(job) || !done) {
        (*callback)(job.id, job.exists);
        ++ids_done;
    }

    while (queue.pop(job)) {
        (*callback)(job.id, job.exists);
        ++ids_done;
    }

    callback->finish(append);
}

struct threaded_callback : public middle_t::cb_func {
    threaded_callback() {}
    void add(output_t *out, int append) {
        m_processors.push_back(boost::make_shared<pending_processor>(out, 4, 100, append));
    }
    bool empty() const {
        return m_processors.empty();
    }
    virtual ~threaded_callback() {}
    int operator()(osmid_t id, int exists) {
        pending_job job;
        job.id = id;
        job.exists = exists;
        BOOST_FOREACH(boost::shared_ptr<pending_processor> &proc, m_processors) {
            proc->submit(job);
        }
        return 0;
    }
    void finish(int exists) {
        BOOST_FOREACH(boost::shared_ptr<pending_processor> &proc, m_processors) {
            proc->join();
        }
        //TODO: Anything else needed here? Individual callback finish method called in threads.
    }
    std::vector<boost::shared_ptr<pending_processor> > m_processors;
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
        threaded_callback callback;
        BOOST_FOREACH(output_t *out, outs) {
            callback.add(out, append);
        }
        if (!callback.empty()) {
            mid->iterate_ways( callback );
            callback.finish(append);

            mid->commit();

            //TODO: Merge things back together

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
