#include "osmdata.hpp"

#include <boost/atomic.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/thread.hpp>
#include <boost/unordered_map.hpp>

#include <stdexcept>

osmdata_t::osmdata_t(boost::shared_ptr<middle_t> mid_, const boost::shared_ptr<output_t>& out_): mid(mid_)
{
    outs.push_back(out_);
}

osmdata_t::osmdata_t(boost::shared_ptr<middle_t> mid_, const std::vector<boost::shared_ptr<output_t> > &outs_)
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
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->node_add(id, lat, lon, tags);
    }
    return status;
}

int osmdata_t::way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    mid->ways_set(id, nodes, node_count, tags);
    
    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->way_add(id, nodes, node_count, tags);
    }
    return status;
}

int osmdata_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    mid->relations_set(id, members, member_count, tags);
    
    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->relation_add(id, members, member_count, tags);
    }
    return status;
}

int osmdata_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->nodes_delete(id);
    slim->nodes_set(id, lat, lon, tags);

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->node_modify(id, lat, lon, tags);
    }

    slim->node_changed(id);

    return status;
}

int osmdata_t::way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->ways_delete(id);
    slim->ways_set(id, nodes, node_count, tags);

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->way_modify(id, nodes, node_count, tags);
    }

    slim->way_changed(id);

    return status;
}

int osmdata_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->relations_delete(id);
    slim->relations_set(id, members, member_count, tags);

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->relation_modify(id, members, member_count, tags);
    }

    slim->relation_changed(id);

    return status;
}

int osmdata_t::node_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->node_delete(id);
    }

    slim->nodes_delete(id);

    return status;
}

int osmdata_t::way_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->way_delete(id);
    }

    slim->ways_delete(id);

    return status;
}

int osmdata_t::relation_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        status |= out->relation_delete(id);
    }

    slim->relations_delete(id);

    return status;
}

void osmdata_t::start() {
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
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

//TODO: have the main thread using the main middle to query the middle for batches of ways (configurable number)
//and stuffing those into the work queue, so we have a single producer multi consumer threaded queue
//since the fetching from middle should be faster than the processing in each backend.

struct pending_threaded_processor : public middle_t::pending_processor {
    typedef std::vector<boost::shared_ptr<output_t> > output_vec_t;
    typedef std::pair<boost::shared_ptr<const middle_query_t>, output_vec_t> clone_t;

    static void do_batch(output_vec_t const& outputs, pending_queue_t& queue, boost::atomic_size_t& ids_done, int append) {
        pending_job_t job;
        while (queue.pop(job)) {
            outputs.at(job.second)->pending_way(job.first, append);
            ++ids_done;
        }
    }

    //starts up count threads and works on the queue
    pending_threaded_processor(boost::shared_ptr<middle_query_t> mid, const output_vec_t& outs, size_t thread_count, size_t job_count, int append)
        : outs(outs), queue(job_count), ids_queued(0), append(append) {

        //nor have we completed any
        ids_done = 0;

        //clone all the things we need
        clones.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            //clone the middle
            boost::shared_ptr<const middle_query_t> mid_clone = mid->get_instance();

            //clone the outs
            output_vec_t out_clones;
            BOOST_FOREACH(const boost::shared_ptr<output_t>& out, outs) {
                out_clones.push_back(out->clone(mid_clone.get()));
            }

            //keep the clones for a specific thread to use
            clones.push_back(clone_t(mid_clone, out_clones));
        }
    }

    ~pending_threaded_processor() {}

    void enqueue(osmid_t id) {
        for(size_t i = 0; i < outs.size(); ++i) {
            outs[i]->enqueue_ways(queue, id, i, ids_queued);
        }
    }

    //waits for the completion of all outstanding jobs
    void process_ways() {
        //reset the number we've done
        ids_done = 0;

        //make the threads and start them
        for (size_t i = 0; i < clones.size(); ++i) {
            workers.create_thread(boost::bind(do_batch, boost::cref(clones[i].second), boost::ref(queue), boost::ref(ids_done), append));
        }

        //TODO: print out progress

        //wait for them to really be done
        workers.join_all();
        ids_queued = 0;

        //collect all the new rels that became pending from each
        //output in each thread back to their respective main outputs
        BOOST_FOREACH(const clone_t& clone, clones) {
            //for each clone/original output
            for(output_vec_t::const_iterator original_output = outs.begin(), clone_output = clone.second.begin();
                original_output != outs.end() && clone_output != clone.second.end(); ++original_output, ++clone_output) {
                //done copying ways for now
                clone_output->get()->commit();
                //merge the pending from this threads copy of output back
                original_output->get()->merge_pending_relations(*clone_output);
            }
        }
    }


    void process_relations() {
        //after all processing


        //TODO: commit all the outputs (will finish the copies and commit the transactions)
        //TODO: collapse the expire_tiles trees
    }

    int thread_count() {
        return clones.size();
    }

    int size() {
        //TODO: queue.size()????
        return 0;
    }

private:
    //middle and output copies
    std::vector<clone_t> clones;
    output_vec_t outs; //would like to move ownership of outs to osmdata_t and middle passed to output_t instead of owned by it
    //actual threads
    boost::thread_group workers;
    //job queue
    pending_queue_t queue;
    //how many ids within the job have been processed
    boost::atomic_size_t ids_done;
    //how many jobs do we have in the queue to start with
    size_t ids_queued;
    //
    int append;

};

} // anonymous namespace

void osmdata_t::stop() {
    /* Commit the transactions, so that multiple processes can
     * access the data simultanious to process the rest in parallel
     * as well as see the newly created tables.
     */
    size_t pending_count = mid->pending_count();
    mid->commit();
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        out->commit();
        pending_count += out->pending_count();
    }

    // should be the same for all outputs
    const int append = outs[0]->get_options()->append;

    //threaded pending processing
    pending_threaded_processor ptp(mid, outs, outs[0]->get_options()->num_procs, pending_count, outs[0]->get_options()->append);

	/* Pending ways
     * This stage takes ways which were processed earlier, but might be
     * involved in a multipolygon relation. They could also be ways that
     * were modified in diff processing.
     */
    if (!outs.empty()) {
        mid->iterate_ways( ptp );
    }

	/* Pending relations
	 * This is like pending ways, except there aren't pending relations
	 * on import, only on update.
	 * TODO: Can we skip this on import?
	 */
    {
        cb_func callback;
        BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
            middle_t::cb_func *rel_callback = out->relation_callback();
            if (rel_callback != NULL) {
                callback.add(rel_callback);
            }
        }
        if (!callback.empty()) {
            mid->iterate_relations( callback );
            callback.finish(append);

            mid->commit();
            BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
                out->commit();
            }
        }
    }

	/* Clustering, index creation, and cleanup.
	 * All the intensive parts of this are long-running PostgreSQL commands
	 */
    mid->stop();
    BOOST_FOREACH(boost::shared_ptr<output_t>& out, outs) {
        out->stop();
    }
}
