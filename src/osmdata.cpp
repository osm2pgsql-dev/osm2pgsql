#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <osmium/thread/pool.hpp>

#include "db-copy.hpp"
#include "format.hpp"
#include "middle.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "util.hpp"

osmdata_t::osmdata_t(std::unique_ptr<dependency_manager_t> dependency_manager,
                     std::shared_ptr<middle_t> mid,
                     std::vector<std::shared_ptr<output_t>> outs)
: m_dependency_manager(std::move(dependency_manager)), m_mid(std::move(mid)),
  m_outs(std::move(outs))
{
    assert(m_dependency_manager);
    assert(m_mid);
    assert(!m_outs.empty());

    // Get the "extra_attributes" option from the first output. We expect
    // all others to be the same.
    m_with_extra_attrs = m_outs[0]->get_options()->extra_attributes;
}

/**
 * For modify and delete member functions a middle_t is not enough, an object
 * of the derived class slim_middle_t is needed. This function does the
 * conversion. It should always succeed, because the modify and delete
 * functions are never called for non-slim middles.
 */
slim_middle_t &osmdata_t::slim_middle() const noexcept
{
    auto *slim = dynamic_cast<slim_middle_t *>(m_mid.get());
    assert(slim);
    return *slim;
}

void osmdata_t::node_add(osmium::Node const &node) const
{
    m_mid->node_set(node);

    if (m_with_extra_attrs || !node.tags().empty()) {
        for (auto &out : m_outs) {
            out->node_add(node);
        }
    }
}

void osmdata_t::way_add(osmium::Way *way) const
{
    m_mid->way_set(*way);

    if (m_with_extra_attrs || !way->tags().empty()) {
        for (auto &out : m_outs) {
            out->way_add(way);
        }
    }
}

void osmdata_t::relation_add(osmium::Relation const &rel) const
{
    m_mid->relation_set(rel);

    if (m_with_extra_attrs || !rel.tags().empty()) {
        for (auto &out : m_outs) {
            out->relation_add(rel);
        }
    }
}

void osmdata_t::node_modify(osmium::Node const &node) const
{
    auto &slim = slim_middle();

    slim.node_delete(node.id());
    slim.node_set(node);

    for (auto &out : m_outs) {
        out->node_modify(node);
    }

    m_dependency_manager->node_changed(node.id());
}

void osmdata_t::way_modify(osmium::Way *way) const
{
    auto &slim = slim_middle();

    slim.way_delete(way->id());
    slim.way_set(*way);

    for (auto &out : m_outs) {
        out->way_modify(way);
    }

    m_dependency_manager->way_changed(way->id());
}

void osmdata_t::relation_modify(osmium::Relation const &rel) const
{
    auto &slim = slim_middle();

    slim.relation_delete(rel.id());
    slim.relation_set(rel);

    for (auto &out : m_outs) {
        out->relation_modify(rel);
    }

    m_dependency_manager->relation_changed(rel.id());
}

void osmdata_t::node_delete(osmid_t id) const
{
    for (auto &out : m_outs) {
        out->node_delete(id);
    }

    slim_middle().node_delete(id);
}

void osmdata_t::way_delete(osmid_t id) const
{
    for (auto &out : m_outs) {
        out->way_delete(id);
    }

    slim_middle().way_delete(id);
}

void osmdata_t::relation_delete(osmid_t id) const
{
    for (auto &out : m_outs) {
        out->relation_delete(id);
    }

    slim_middle().relation_delete(id);

    m_dependency_manager->relation_deleted(id);
}

void osmdata_t::start() const
{
    for (auto &out : m_outs) {
        out->start();
    }
}

void osmdata_t::flush() const
{
    m_mid->flush();
}

namespace {

/**
 * After all objects in a change file have been processed, all objects
 * depending on the changed objects must also be processed. This class
 * handles this extra processing by starting a number of threads and doing
 * the processing in them.
 */
class multithreaded_processor
{
public:
    using output_vec_t = std::vector<std::shared_ptr<output_t>>;

    multithreaded_processor(std::shared_ptr<middle_t> const &mid,
                            output_vec_t outs, size_t thread_count)
    : m_outputs(std::move(outs))
    {
        assert(!m_outputs.empty());

        // The database connection info should be the same for all outputs,
        // we take it arbitrarily from the first.
        std::string const &conninfo =
            m_outputs[0]->get_options()->database_options.conninfo();

        // For each thread we create clones of all the outputs.
        m_clones.resize(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            auto const midq = mid->get_query_instance();
            auto copy_thread = std::make_shared<db_copy_thread_t>(conninfo);

            for (auto const &out : m_outputs) {
                if (out->need_forward_dependencies()) {
                    m_clones[i].push_back(out->clone(midq, copy_thread));
                } else {
                    m_clones[i].emplace_back(nullptr);
                }
            }
        }
    }

    /**
     * Process all ways in the list.
     *
     * \param list List of way ids to work on. The list is moved into the
     *             function.
     */
    void process_ways(idlist_t &&list)
    {
        process_queue("way", std::move(list), do_ways);
    }

    /**
     * Process all relations in the list.
     *
     * \param list List of relation ids to work on. The list is moved into the
     *             function.
     */
    void process_relations(idlist_t &&list)
    {
        process_queue("relation", std::move(list), do_rels);

        // Collect expiry tree information from all clones and merge it back
        // into the original outputs.
        for (auto const &clone : m_clones) {
            auto it = clone.begin();
            for (auto const &output : m_outputs) {
                assert(it != clone.end());
                if (*it) {
                    output->merge_expire_trees(it->get());
                }
                ++it;
            }
        }
    }

private:
    /// Get the next id from the queue.
    static osmid_t pop_id(idlist_t *queue, std::mutex *mutex)
    {
        osmid_t id = 0;

        std::lock_guard<std::mutex> const lock{*mutex};
        if (!queue->empty()) {
            id = queue->back();
            queue->pop_back();
        }

        return id;
    }

    /**
     * Runs in the worker threads: As long as there are any, get ids from
     * the queue and let the outputs process the ways.
     */
    static void do_ways(output_vec_t const &outputs, idlist_t *queue,
                        std::mutex *mutex)
    {
        while (osmid_t const id = pop_id(queue, mutex)) {
            for (auto const &output : outputs) {
                if (output) {
                    output->pending_way(id);
                }
            }
        }
    }

    /**
     * Runs in the worker threads: As long as there are any, get ids from
     * the queue and let the outputs process the relations.
     */
    static void do_rels(output_vec_t const &outputs, idlist_t *queue,
                        std::mutex *mutex)
    {
        while (osmid_t const id = pop_id(queue, mutex)) {
            for (auto const &output : outputs) {
                if (output) {
                    output->pending_relation(id);
                }
            }
        }
    }

    /// Runs in a worker thread: Update progress display once per second.
    static void print_stats(idlist_t *queue, std::mutex *mutex)
    {
        size_t queue_size;
        do {
            mutex->lock();
            queue_size = queue->size();
            mutex->unlock();

            fmt::print(stderr, "\rLeft to process: {}...", queue_size);

            std::this_thread::sleep_for(std::chrono::seconds{1});
        } while (queue_size > 0);
    }

    template <typename FUNCTION>
    void process_queue(char const *type, idlist_t list, FUNCTION &&function)
    {
        auto const ids_queued = list.size();

        fmt::print(stderr, "\nGoing over pending {}s...\n", type);
        fmt::print(stderr, "\t{} {}s are pending\n", ids_queued, type);
        fmt::print(stderr, "\nUsing {} helper-processes\n", m_clones.size());

        util::timer_t timer;
        std::vector<std::future<void>> workers;

        for (auto const &clone : m_clones) {
            workers.push_back(std::async(
                std::launch::async, std::forward<FUNCTION>(function),
                std::cref(clone), &list, &m_mutex));
        }
        workers.push_back(std::async(std::launch::async, print_stats,
                                     &list, &m_mutex));

        for (auto &worker : workers) {
            try {
                worker.get();
            } catch (...) {
                // Drain the queue, so that the other workers finish early.
                m_mutex.lock();
                list.clear();
                m_mutex.unlock();
                throw;
            }
        }

        for (auto const &clone : m_clones) {
            for (auto const &clone_output : clone) {
                if (clone_output) {
                    clone_output->commit();
                }
            }
        }

        timer.stop();

        fmt::print(stderr, "\rFinished processing {} {}s in {} s\n\n",
                   ids_queued, type, timer.elapsed());

        if (timer.elapsed() > 0) {
            fmt::print(stderr,
                       "{} pending {}s took {}s at a rate of {:.2f}/s\n",
                       ids_queued, type, timer.elapsed(),
                       timer.per_second(ids_queued));
        }
    }

    /// Clones of all outputs, one vector of clones per thread.
    std::vector<output_vec_t> m_clones;

    /// All outputs.
    output_vec_t m_outputs;

    /// Mutex to make sure worker threads coordinate access to queue.
    std::mutex m_mutex;
};

} // anonymous namespace

void osmdata_t::stop() const
{
    /* Commit the transactions, so that multiple processes can
     * access the data simultaneously to process the rest in parallel
     * as well as see the newly created tables.
     */
    m_mid->commit();
    for (auto &out : m_outs) {
        //TODO: each of the outs can be in parallel
        out->commit();
    }

    // should be the same for all outputs
    auto const *opts = m_outs[0]->get_options();

    // In append mode there might be dependent objects pending that we
    // need to process.
    if (opts->append && m_dependency_manager->has_pending()) {
        multithreaded_processor proc{m_mid, m_outs,
                                     (std::size_t)opts->num_procs};

        proc.process_ways(m_dependency_manager->get_pending_way_ids());
        proc.process_relations(m_dependency_manager->get_pending_relation_ids());
    }

    for (auto &out : m_outs) {
        out->stage2_proc();
    }

    // Clustering, index creation, and cleanup.
    // All the intensive parts of this are long-running PostgreSQL commands
    {
        osmium::thread::Pool pool{opts->parallel_indexing ? opts->num_procs : 1,
                                  512};

        if (opts->droptemp) {
            // When dropping middle tables, make sure they are gone before
            // indexing starts.
            m_mid->stop(pool);
        }

        for (auto &out : m_outs) {
            out->stop(&pool);
        }

        if (!opts->droptemp) {
            // When keeping middle tables, there is quite a large index created
            // which is better done after the output tables have been copied.
            // Note that --disable-parallel-indexing needs to be used to really
            // force the order.
            m_mid->stop(pool);
        }

        // Waiting here for pool to execute all tasks.
        // XXX If one of them has an error, all other will finish first,
        //     which may take a long time.
    }
}
