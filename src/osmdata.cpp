#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include "check-order.hpp"
#include "db-copy.hpp"
#include "format.hpp"
#include "input-handler.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "thread-pool.hpp"
#include "util.hpp"

osmdata_t::osmdata_t(std::unique_ptr<dependency_manager_t> dependency_manager,
                     std::shared_ptr<middle_t> mid,
                     std::vector<std::shared_ptr<output_t>> outs,
                     options_t const &options)
: m_dependency_manager(std::move(dependency_manager)), m_mid(std::move(mid)),
  m_outs(std::move(outs)), m_conninfo(options.database_options.conninfo()),
  m_num_procs(options.num_procs), m_append(options.append),
  m_droptemp(options.droptemp), m_parallel_indexing(options.parallel_indexing),
  m_with_extra_attrs(options.extra_attributes),
  m_with_forward_dependencies(options.with_forward_dependencies)
{
    assert(m_dependency_manager);
    assert(m_mid);
    assert(!m_outs.empty());
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

    for (auto &out : m_outs) {
        out->select_relation_members(rel.id());
    }

    slim.relation_delete(rel.id());
    slim.relation_set(rel);

    for (auto &out : m_outs) {
        out->relation_modify(rel);
    }
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

    multithreaded_processor(std::string const &conninfo,
                            std::shared_ptr<middle_t> const &mid,
                            output_vec_t outs, size_t thread_count)
    : m_outputs(std::move(outs))
    {
        assert(!m_outputs.empty());

        // For each thread we create clones of all the outputs.
        m_clones.resize(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            auto const midq = mid->get_query_instance();
            auto copy_thread = std::make_shared<db_copy_thread_t>(conninfo);

            for (auto const &out : m_outputs) {
                m_clones[i].push_back(out->clone(midq, copy_thread));
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
        process_queue("way", std::move(list), &output_t::pending_way);
    }

    /**
     * Process all relations in the list.
     *
     * \param list List of relation ids to work on. The list is moved into the
     *             function.
     */
    void process_relations(idlist_t &&list)
    {
        process_queue("relation", std::move(list), &output_t::pending_relation);
    }

    /**
     * Process all relations in the list in stage1c.
     *
     * \param list List of relation ids to work on. The list is moved into the
     *             function.
     */
    void process_relations_stage1c(idlist_t &&list)
    {
        process_queue("relation", std::move(list),
                      &output_t::pending_relation_stage1c);
    }

    /**
     * Collect expiry tree information from all clones and merge it back
     * into the original outputs.
     */
    void merge_expire_trees()
    {
        std::size_t n = 0;
        for (auto const &output : m_outputs) {
            for (auto const &clone : m_clones) {
                assert(n < clone.size());
                output->merge_expire_trees(clone[n].get());
            }
            ++n;
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

    // Pointer to a member function of output_t taking an osm_id
    using output_member_fn_ptr = void (output_t::*)(osmid_t);

    /**
     * Runs in the worker threads: As long as there are any, get ids from
     * the queue and let the outputs process it by calling "func".
     */
    static void run(output_vec_t const &outputs, idlist_t *queue,
                    std::mutex *mutex, output_member_fn_ptr func)
    {
        while (osmid_t const id = pop_id(queue, mutex)) {
            for (auto const &output : outputs) {
                if (output) {
                    (output.get()->*func)(id);
                }
            }
        }
        for (auto const &output : outputs) {
            if (output) {
                output->sync();
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

            if (get_logger().show_progress()) {
                fmt::print(stderr, "\rLeft to process: {}...", queue_size);
            }

            std::this_thread::sleep_for(std::chrono::seconds{1});
        } while (queue_size > 0);
    }

    void process_queue(char const *type, idlist_t list,
                       output_member_fn_ptr function)
    {
        auto const ids_queued = list.size();

        if (get_logger().show_progress()) {
            fmt::print(stderr, "\nGoing over pending {}s...\n", type);
            fmt::print(stderr, "\t{} {}s are pending\n", ids_queued, type);
            fmt::print(stderr, "\nUsing {} helper-processes\n",
                       m_clones.size());
        }

        util::timer_t timer;
        std::vector<std::future<void>> workers;

        for (auto const &clone : m_clones) {
            workers.push_back(std::async(std::launch::async, run,
                                         std::cref(clone), &list, &m_mutex,
                                         function));
        }
        workers.push_back(
            std::async(std::launch::async, print_stats, &list, &m_mutex));

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

        timer.stop();

        if (get_logger().show_progress()) {
            fmt::print(stderr, "\rFinished processing {} {}s in {}\n\n",
                       ids_queued, type,
                       util::human_readable_duration(timer.elapsed()));
        }

        if (timer.elapsed() > 0) {
            log_info("{} pending {}s took {} at a rate of {:.2f}/s", ids_queued,
                     type, util::human_readable_duration(timer.elapsed()),
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

progress_display_t osmdata_t::process_file(osmium::io::File const &file,
                                           osmium::Box const &bbox) const
{
    if (!m_append && file.has_multiple_object_versions()) {
        throw std::runtime_error{
            "Reading an OSM change file only works in append mode."};
    }

    log_info("Using {} parser.", osmium::io::as_string(file.format()));

    check_order_t check_order{};
    input_handler_t handler{bbox, m_append, this};
    osmium::io::Reader reader{file};
    osmium::apply(reader, check_order, handler);
    reader.close();

    return handler.progress();
}

void osmdata_t::process_dependents() const
{
    multithreaded_processor proc{m_conninfo, m_mid, m_outs,
                                 (std::size_t)m_num_procs};

    // stage 1b processing: process parents of changed objects
    if (m_dependency_manager->has_pending()) {
        proc.process_ways(m_dependency_manager->get_pending_way_ids());
        proc.process_relations(
            m_dependency_manager->get_pending_relation_ids());
        proc.merge_expire_trees();
    }

    // stage 1c processing: mark parent relations of marked objects as changed
    for (auto &out : m_outs) {
        for (auto const id : out->get_marked_way_ids()) {
            m_dependency_manager->way_changed(id);
        }
    }

    // process parent relations of marked ways
    if (m_dependency_manager->has_pending()) {
        proc.process_relations_stage1c(
            m_dependency_manager->get_pending_relation_ids());
    }
}

void osmdata_t::reprocess_marked() const
{
    for (auto &out : m_outs) {
        out->reprocess_marked();
    }
}

void osmdata_t::postprocess_database() const
{
    // All the intensive parts of this are long-running PostgreSQL commands.
    // They will be run in a thread pool.
    thread_pool_t pool{m_parallel_indexing ? m_num_procs : 1};

    if (m_droptemp) {
        // When dropping middle tables, make sure they are gone before
        // indexing starts.
        m_mid->stop(pool);
    }

    for (auto &out : m_outs) {
        out->stop(&pool);
    }

    if (!m_droptemp) {
        // When keeping middle tables, there is quite a large index created
        // which is better done after the output tables have been copied.
        // Note that --disable-parallel-indexing needs to be used to really
        // force the order.
        m_mid->stop(pool);
    }

    // Waiting here for pool to execute all tasks. If one of them throws an
    // exception, this will throw.
    pool.check_for_exceptions();
}

void osmdata_t::stop() const
{
    /* Commit the transactions, so that multiple processes can
     * access the data simultaneously to process the rest in parallel
     * as well as see the newly created tables.
     */
    m_mid->commit();
    for (auto &out : m_outs) {
        out->sync();
    }

    if (m_append && m_with_forward_dependencies) {
        process_dependents();
    }

    reprocess_marked();

    postprocess_database();
}
