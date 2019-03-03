#include <cstdio>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <osmium/thread/pool.hpp>

#include "db-copy.hpp"
#include "middle.hpp"
#include "node-ram-cache.hpp"
#include "osmdata.hpp"
#include "output.hpp"

osmdata_t::osmdata_t(std::shared_ptr<middle_t> mid_,
                     std::shared_ptr<output_t> const &out_)
: mid(mid_)
{
    outs.push_back(out_);
    with_extra = outs[0]->get_options()->extra_attributes;
}

osmdata_t::osmdata_t(std::shared_ptr<middle_t> mid_,
                     std::vector<std::shared_ptr<output_t>> const &outs_)
: mid(mid_), outs(outs_)
{
    if (outs.empty()) {
        throw std::runtime_error("Must have at least one output, but none have "
                                 "been configured.");
    }

    with_extra = outs[0]->get_options()->extra_attributes;
}

int osmdata_t::node_add(osmium::Node const &node)
{
    mid->nodes_set(node);

    int status = 0;

    if (with_extra || !node.tags().empty()) {
        for (auto &out : outs) {
            status |= out->node_add(node);
        }
    }

    return status;
}

int osmdata_t::way_add(osmium::Way *way)
{
    mid->ways_set(*way);

    int status = 0;

    if (with_extra || !way->tags().empty()) {
        for (auto& out: outs) {
            status |= out->way_add(way);
        }
    }

    return status;
}

int osmdata_t::relation_add(osmium::Relation const &rel)
{
    mid->relations_set(rel);

    int status = 0;
    if (with_extra || !rel.tags().empty()) {
        for (auto& out: outs) {
            status |= out->relation_add(rel);
        }
    }

    return status;
}

int osmdata_t::node_modify(osmium::Node const &node)
{
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->nodes_delete(node.id());
    slim->nodes_set(node);

    int status = 0;
    for (auto& out: outs) {
        status |= out->node_modify(node);
    }

    slim->node_changed(node.id());

    return status;
}

int osmdata_t::way_modify(osmium::Way *way)
{
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->ways_delete(way->id());
    slim->ways_set(*way);

    int status = 0;
    for (auto& out: outs) {
        status |= out->way_modify(way);
    }

    slim->way_changed(way->id());

    return status;
}

int osmdata_t::relation_modify(osmium::Relation const &rel)
{
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    slim->relations_delete(rel.id());
    slim->relations_set(rel);

    int status = 0;
    for (auto& out: outs) {
        status |= out->relation_modify(rel);
    }

    slim->relation_changed(rel.id());

    return status;
}

int osmdata_t::node_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    for (auto& out: outs) {
        status |= out->node_delete(id);
    }

    slim->nodes_delete(id);

    return status;
}

int osmdata_t::way_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    for (auto& out: outs) {
        status |= out->way_delete(id);
    }

    slim->ways_delete(id);

    return status;
}

int osmdata_t::relation_delete(osmid_t id) {
    slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid.get());

    int status = 0;
    for (auto& out: outs) {
        status |= out->relation_delete(id);
    }

    slim->relations_delete(id);

    return status;
}

void osmdata_t::start() {
    for (auto& out: outs) {
        out->start();
    }
}

void osmdata_t::type_changed(osmium::item_type new_type)
{
    mid->flush(new_type);
}

namespace {

//TODO: have the main thread using the main middle to query the middle for batches of ways (configurable number)
//and stuffing those into the work queue, so we have a single producer multi consumer threaded queue
//since the fetching from middle should be faster than the processing in each backend.

struct pending_threaded_processor : public middle_t::pending_processor {
    using output_vec_t = std::vector<std::shared_ptr<output_t>>;

    static void do_jobs(output_vec_t const& outputs, pending_queue_t& queue, size_t& ids_done, std::mutex& mutex, int append, bool ways) {
        while (true) {
            //get the job off the queue synchronously
            pending_job_t job;
            mutex.lock();
            if(queue.empty()) {
                mutex.unlock();
                break;
            }
            else {
                job = queue.top();
                queue.pop();
            }
            mutex.unlock();

            //process it
            if(ways)
                outputs.at(job.output_id)->pending_way(job.osm_id, append);
            else
                outputs.at(job.output_id)->pending_relation(job.osm_id, append);

            mutex.lock();
            ++ids_done;
            mutex.unlock();
        }
    }

    static void print_stats(pending_queue_t &queue, std::mutex &mutex)
    {
        size_t queue_size;
        do {
            mutex.lock();
            queue_size = queue.size();
            mutex.unlock();

            fprintf(stderr, "\rLeft to process: %zu...", queue_size);

            std::this_thread::sleep_for(std::chrono::seconds(1));
        } while (queue_size > 0);
    }

    //starts up count threads and works on the queue
    pending_threaded_processor(std::shared_ptr<middle_t> mid,
                               const output_vec_t &outs, size_t thread_count,
                               int append)
        //note that we cant hint to the stack how large it should be ahead of time
        //we could use a different datastructure like a deque or vector but then
        //the outputs the enqueue jobs would need the version check for the push(_back) method
        : outs(outs),
          ids_queued(0),
          append(append),
          queue(),
          ids_done(0)
    {

        //clone all the things we need
        clones.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            //clone the middle
            auto mid_clone = mid->get_query_instance(mid);
            auto copy_thread = std::make_shared<db_copy_thread_t>(
                outs[0]->get_options()->database_options.conninfo());

            //clone the outs
            output_vec_t out_clones;
            for (const auto& out: outs) {
                out_clones.push_back(out->clone(mid_clone, copy_thread));
            }

            //keep the clones for a specific thread to use
            clones.push_back(out_clones);
        }
    }

    ~pending_threaded_processor() {}

    void enqueue_ways(osmid_t id) {
        for(size_t i = 0; i < outs.size(); ++i) {
            outs[i]->enqueue_ways(queue, id, i, ids_queued);
        }
    }

    //waits for the completion of all outstanding jobs
    void process_ways() {
        //reset the number we've done
        ids_done = 0;

        fprintf(stderr, "\nGoing over pending ways...\n");
        fprintf(stderr, "\t%zu ways are pending\n", ids_queued);
        fprintf(stderr, "\nUsing %zu helper-processes\n", clones.size());
        time_t start = time(nullptr);


        //make the threads and start them
        std::vector<std::future<void>> workers;
        for (auto const &clone : clones) {
            workers.push_back(std::async(
                std::launch::async, do_jobs, std::cref(clone), std::ref(queue),
                std::ref(ids_done), std::ref(mutex), append, true));
        }
        workers.push_back(std::async(std::launch::async, print_stats,
                                     std::ref(queue), std::ref(mutex)));

        for (auto& w: workers) {
            try {
                w.get();
            } catch (...) {
                // drain the queue, so that the other workers finish
                mutex.lock();
                while (!queue.empty()) {
                    queue.pop();
                }
                mutex.unlock();
                throw;
            }
        }

        time_t finish = time(nullptr);
        fprintf(stderr, "\rFinished processing %zu ways in %i s\n\n", ids_queued, (int)(finish - start));
        if (finish - start > 0)
            fprintf(stderr, "%zu Pending ways took %ds at a rate of %.2f/s\n", ids_queued, (int)(finish - start),
                    ((double)ids_queued / (double)(finish - start)));
        ids_queued = 0;
        ids_done = 0;

        //collect all the new rels that became pending from each
        //output in each thread back to their respective main outputs
        for (auto const &clone : clones) {
            //for each clone/original output
            for (output_vec_t::const_iterator original_output = outs.begin(),
                                              clone_output = clone.begin();
                 original_output != outs.end() && clone_output != clone.end();
                 ++original_output, ++clone_output) {
                //done copying ways for now
                clone_output->get()->commit();
                //merge the pending from this threads copy of output back
                original_output->get()->merge_pending_relations(clone_output->get());
            }
        }
    }

    void enqueue_relations(osmid_t id) {
        for(size_t i = 0; i < outs.size(); ++i) {
            outs[i]->enqueue_relations(queue, id, i, ids_queued);
        }
    }

    void process_relations() {
        //reset the number we've done
        ids_done = 0;

        fprintf(stderr, "\nGoing over pending relations...\n");
        fprintf(stderr, "\t%zu relations are pending\n", ids_queued);
        fprintf(stderr, "\nUsing %zu helper-processes\n", clones.size());
        time_t start = time(nullptr);

        //make the threads and start them
        std::vector<std::future<void>> workers;
        for (auto const &clone : clones) {
            workers.push_back(std::async(
                std::launch::async, do_jobs, std::cref(clone), std::ref(queue),
                std::ref(ids_done), std::ref(mutex), append, false));
        }
        workers.push_back(std::async(std::launch::async, print_stats,
                                     std::ref(queue), std::ref(mutex)));

        for (auto& w: workers) {
            try {
                w.get();
            } catch (...) {
                // drain the queue, so the other worker finish immediately
                mutex.lock();
                while (!queue.empty()) {
                    queue.pop();
                }
                mutex.unlock();
                throw;
            }
        }

        time_t finish = time(nullptr);
        fprintf(stderr, "\rFinished processing %zu relations in %i s\n\n", ids_queued, (int)(finish - start));
        if (finish - start > 0)
            fprintf(stderr, "%zu Pending relations took %ds at a rate of %.2f/s\n", ids_queued, (int)(finish - start),
                    ((double)ids_queued / (double)(finish - start)));
        ids_queued = 0;
        ids_done = 0;

        //collect all expiry tree informations together into one
        for (auto const &clone : clones) {
            //for each clone/original output
            for (output_vec_t::const_iterator original_output = outs.begin(),
                                              clone_output = clone.begin();
                 original_output != outs.end() && clone_output != clone.end();
                 ++original_output, ++clone_output) {
                //done copying rels for now
                clone_output->get()->commit();
                //merge the expire tree from this threads copy of output back
                original_output->get()->merge_expire_trees(clone_output->get());
            }
        }
    }

private:
    // output copies, one vector per thread
    std::vector<output_vec_t> clones;
    output_vec_t outs; //would like to move ownership of outs to osmdata_t and middle passed to output_t instead of owned by it
    //how many jobs do we have in the queue to start with
    size_t ids_queued;
    //appending to output that is already there (diff processing)
    bool append;
    //job queue
    pending_queue_t queue;

    //how many ids within the job have been processed
    size_t ids_done;
    //so the threads can manage some of the shared state
    std::mutex mutex;
};

} // anonymous namespace

void osmdata_t::stop() {
    /* Commit the transactions, so that multiple processes can
     * access the data simultanious to process the rest in parallel
     * as well as see the newly created tables.
     */
    mid->commit();
    for (auto& out: outs) {
        //TODO: each of the outs can be in parallel
        out->commit();
    }

    // should be the same for all outputs
    auto *opts = outs[0]->get_options();

    // are there any objects left pending?
    bool has_pending = mid->pending_count() > 0;
    for (auto const &out : outs) {
        has_pending |= out->pending_count() > 0;
    }

    if (has_pending) {
        //threaded pending processing
        pending_threaded_processor ptp(mid, outs, opts->num_procs,
                                       opts->append);

        if (!outs.empty()) {
            //This stage takes ways which were processed earlier, but might be
            //involved in a multipolygon relation. They could also be ways that
            //were modified in diff processing.
            mid->iterate_ways(ptp);

            //This is like pending ways, except there aren't pending relations
            //on import, only on update.
            //TODO: Can we skip this on import?
            mid->iterate_relations(ptp);
        }
    }


    // Clustering, index creation, and cleanup.
    // All the intensive parts of this are long-running PostgreSQL commands
    {
        osmium::thread::Pool pool(opts->parallel_indexing ? opts->num_procs : 1,
                                  512);

        if (opts->droptemp) {
            // When dropping middle tables, make sure they are gone before
            // indexing starts.
            mid->stop(pool);
        }

        for (auto &out : outs) {
            out->stop(&pool);
        }

        if (!opts->droptemp) {
            // When keeping middle tables, there is quite a large index created
            // which is better done after the output tables have been copied.
            // Note that --disable-parallel-indexing needs to be used to really
            // force the order.
            mid->stop(pool);
        }

        // Waiting here for pool to execute all tasks.
        // XXX If one of them has an error, all other will finish first,
        //     which may take a long time.
    }
}
