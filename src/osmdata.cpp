/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "db-copy.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "util.hpp"

osmdata_t::osmdata_t(std::unique_ptr<dependency_manager_t> dependency_manager,
                     std::shared_ptr<middle_t> mid,
                     std::shared_ptr<output_t> output, options_t const &options)
: m_dependency_manager(std::move(dependency_manager)), m_mid(std::move(mid)),
  m_output(std::move(output)), m_conninfo(options.database_options.conninfo()),
  m_bbox(options.bbox), m_num_procs(options.num_procs),
  m_append(options.append), m_droptemp(options.droptemp),
  m_with_extra_attrs(options.extra_attributes),
  m_with_forward_dependencies(options.with_forward_dependencies)
{
    assert(m_dependency_manager);
    assert(m_mid);
    assert(m_output);
}

void osmdata_t::node(osmium::Node const &node)
{
    if (node.visible()) {
        if (!node.location().valid()) {
            log_warn("Ignored node {} (version {}) with invalid location.",
                     node.id(), node.version());
            return;
        }
        if (m_bbox.valid() && !m_bbox.contains(node.location())) {
            return;
        }
    }

    m_mid->node(node);

    if (node.deleted()) {
        node_delete(node.id());
    } else {
        if (m_append) {
            node_modify(node);
        } else {
            node_add(node);
        }
    }
}

void osmdata_t::after_nodes() { m_mid->after_nodes(); }

void osmdata_t::way(osmium::Way &way)
{
    m_mid->way(way);

    if (way.deleted()) {
        way_delete(way.id());
    } else {
        if (m_append) {
            way_modify(&way);
        } else {
            way_add(&way);
        }
    }
}

void osmdata_t::after_ways() { m_mid->after_ways(); }

void osmdata_t::relation(osmium::Relation const &rel)
{
    if (rel.members().size() > 32767) {
        log_warn(
            "Relation id {} ignored, because it has more than 32767 members",
            rel.id());
        return;
    }

    if (m_append && !rel.deleted()) {
        m_output->select_relation_members(rel.id());
    }

    m_mid->relation(rel);

    if (rel.deleted()) {
        relation_delete(rel.id());
    } else {
        if (m_append) {
            relation_modify(rel);
        } else {
            relation_add(rel);
        }
    }
}

void osmdata_t::after_relations() { m_mid->after_relations(); }

void osmdata_t::node_add(osmium::Node const &node) const
{
    if (m_with_extra_attrs || !node.tags().empty()) {
        m_output->node_add(node);
    }
}

void osmdata_t::way_add(osmium::Way *way) const
{
    if (m_with_extra_attrs || !way->tags().empty()) {
        m_output->way_add(way);
    }
}

void osmdata_t::relation_add(osmium::Relation const &rel) const
{
    if (m_with_extra_attrs || !rel.tags().empty()) {
        m_output->relation_add(rel);
    }
}

void osmdata_t::node_modify(osmium::Node const &node) const
{
    m_output->node_modify(node);
    m_dependency_manager->node_changed(node.id());
}

void osmdata_t::way_modify(osmium::Way *way) const
{
    m_output->way_modify(way);
    m_dependency_manager->way_changed(way->id());
}

void osmdata_t::relation_modify(osmium::Relation const &rel) const
{
    m_output->relation_modify(rel);
}

void osmdata_t::node_delete(osmid_t id) const
{
    m_output->node_delete(id);
}

void osmdata_t::way_delete(osmid_t id) const
{
    m_output->way_delete(id);
}

void osmdata_t::relation_delete(osmid_t id) const
{
    m_output->relation_delete(id);
}

void osmdata_t::start() const
{
    m_output->start();
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
    multithreaded_processor(std::string const &conninfo,
                            std::shared_ptr<middle_t> const &mid,
                            std::shared_ptr<output_t> output,
                            std::size_t thread_count)
    : m_output(std::move(output))
    {
        assert(mid);
        assert(m_output);

        // For each thread we create a clone of the output.
        for (std::size_t i = 0; i < thread_count; ++i) {
            auto const midq = mid->get_query_instance();
            auto copy_thread = std::make_shared<db_copy_thread_t>(conninfo);
            m_clones.push_back(m_output->clone(midq, copy_thread));
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
     * into the original output.
     */
    void merge_expire_trees()
    {
        for (auto const &clone : m_clones) {
            m_output->merge_expire_trees(clone.get());
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
     * the queue and let the output process it by calling "func".
     */
    static void run(std::shared_ptr<output_t> const &output, idlist_t *queue,
                    std::mutex *mutex, output_member_fn_ptr func)
    {
        while (osmid_t const id = pop_id(queue, mutex)) {
            (output.get()->*func)(id);
        }
        output->sync();
    }

    /// Runs in a worker thread: Update progress display once per second.
    static void print_stats(idlist_t *queue, std::mutex *mutex)
    {
        std::size_t queue_size = 0;
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

        log_info("Going over {} pending {}s (using {} threads)"_format(
            ids_queued, type, m_clones.size()));

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
            fmt::print(stderr, "\rLeft to process: 0.\n");
        }

        log_info("Processing {} pending {}s took {} at a rate of {:.2f}/s",
                 ids_queued, type,
                 util::human_readable_duration(timer.elapsed()),
                 timer.per_second(ids_queued));
    }

    /// Clones of output, one clone per thread.
    std::vector<std::shared_ptr<output_t>> m_clones;

    /// The output.
    std::shared_ptr<output_t> m_output;

    /// Mutex to make sure worker threads coordinate access to queue.
    std::mutex m_mutex;
};

} // anonymous namespace

void osmdata_t::process_dependents() const
{
    multithreaded_processor proc{m_conninfo, m_mid, m_output, m_num_procs};

    // stage 1b processing: process parents of changed objects
    if (m_dependency_manager->has_pending()) {
        proc.process_ways(m_dependency_manager->get_pending_way_ids());
        proc.process_relations(
            m_dependency_manager->get_pending_relation_ids());
        proc.merge_expire_trees();
    }

    // stage 1c processing: mark parent relations of marked objects as changed
    for (auto const id : m_output->get_marked_way_ids()) {
        m_dependency_manager->way_changed(id);
    }

    // process parent relations of marked ways
    if (m_dependency_manager->has_pending()) {
        proc.process_relations_stage1c(
            m_dependency_manager->get_pending_relation_ids());
    }
}

void osmdata_t::reprocess_marked() const { m_output->reprocess_marked(); }

void osmdata_t::postprocess_database() const
{
    m_output->free_middle_references();

    if (m_droptemp) {
        // When dropping middle tables, make sure they are gone before
        // indexing starts.
        m_mid->stop();
    }

    m_output->stop();

    if (!m_droptemp) {
        // When keeping middle tables, there is quite a large index created
        // which is better done after the output tables have been copied.
        // Note that --disable-parallel-indexing needs to be used to really
        // force the order.
        m_mid->stop();
    }

    // Waiting here for pool to execute all tasks.
    m_mid->wait();
    m_output->wait();
}

void osmdata_t::stop() const
{
    m_output->sync();

    if (m_append && m_with_forward_dependencies) {
        process_dependents();
    }

    reprocess_marked();

    postprocess_database();
}
