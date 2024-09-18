/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
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

osmdata_t::osmdata_t(std::shared_ptr<middle_t> mid,
                     std::shared_ptr<output_t> output, options_t const &options)
: m_mid(std::move(mid)), m_output(std::move(output)),
  m_connection_params(options.connection_params), m_bbox(options.bbox),
  m_num_procs(options.num_procs), m_append(options.append),
  m_droptemp(options.droptemp),
  m_with_extra_attrs(options.extra_attributes ||
                     options.output_backend == "flex")
{
    assert(m_mid);
    assert(m_output);
    m_output->start();
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
        m_output->node_delete(node.id());
        return;
    }

    bool const has_tags_or_attrs = m_with_extra_attrs || !node.tags().empty();
    if (m_append) {
        if (has_tags_or_attrs) {
            m_output->node_modify(node);
        } else {
            m_output->node_delete(node.id());
        }

        // Version 1 means this is a new node, so there can't be an existing
        // way or relation referencing it, so we don't have to add that node
        // to the list of changed nodes. If the input data doesn't contain
        // object versions this will still work, because then the version is 0.
        if (node.version() != 1) {
            m_changed_nodes.push_back(node.id());
        }
    } else if (has_tags_or_attrs) {
        m_output->node_add(node);
    }
}

void osmdata_t::after_nodes()
{
    m_mid->after_nodes();
    m_output->after_nodes();

    if (!m_append) {
        return;
    }

    if (!m_changed_nodes.empty()) {
        m_mid->get_node_parents(m_changed_nodes, &m_ways_pending_tracker,
                                &m_rels_pending_tracker);
        m_changed_nodes.clear();
    }
}

void osmdata_t::way(osmium::Way &way)
{
    m_mid->way(way);

    if (way.deleted()) {
        m_output->way_delete(way.id());
        return;
    }

    bool const has_tags_or_attrs = m_with_extra_attrs || !way.tags().empty();
    if (m_append) {
        if (has_tags_or_attrs) {
            m_output->way_modify(&way);
        } else {
            m_output->way_delete(way.id());
        }

        // Version 1 means this is a new way, so there can't be an existing
        // relation referencing it, so we don't have to add that way to the
        // list of changed ways. If the input data doesn't contain object
        // versions this will still work, because then the version is 0.
        if (way.version() != 1) {
            m_changed_ways.push_back(way.id());
        }
    } else if (has_tags_or_attrs) {
        m_output->way_add(&way);
    }
}

void osmdata_t::after_ways()
{
    m_mid->after_ways();
    m_output->after_ways();

    if (!m_append) {
        return;
    }

    if (!m_changed_ways.empty()) {
        if (!m_ways_pending_tracker.empty()) {
            // Remove ids from changed ways in the input data from
            // m_ways_pending_tracker, because they have already been
            // processed.
            m_ways_pending_tracker.remove_ids_if_in(m_changed_ways);

            // Add the list of pending way ids to the list of changed ways,
            // because we need the parents for them, too.
            m_changed_ways.merge_sorted(m_ways_pending_tracker);
        }

        m_mid->get_way_parents(m_changed_ways, &m_rels_pending_tracker);

        m_changed_ways.clear();
        return;
    }

    if (!m_ways_pending_tracker.empty()) {
        m_mid->get_way_parents(m_ways_pending_tracker, &m_rels_pending_tracker);
    }
}

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
        m_output->relation_delete(rel.id());
        return;
    }

    bool const has_tags_or_attrs = m_with_extra_attrs || !rel.tags().empty();
    if (m_append) {
        if (has_tags_or_attrs) {
            m_output->relation_modify(rel);
        } else {
            m_output->relation_delete(rel.id());
        }
        m_changed_relations.push_back(rel.id());
    } else if (has_tags_or_attrs) {
        m_output->relation_add(rel);
    }
}

void osmdata_t::after_relations()
{
    m_mid->after_relations();
    m_output->after_relations();

    if (m_append) {
        // Remove ids from changed relations in the input data from
        // m_rels_pending_tracker, because they have already been processed.
        m_rels_pending_tracker.remove_ids_if_in(m_changed_relations);

        m_changed_relations.clear();
    }

    m_output->sync();
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
    multithreaded_processor(connection_params_t const &connection_params,
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
            auto copy_thread =
                std::make_shared<db_copy_thread_t>(connection_params);
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
            id = queue->pop_id();
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

        util::timer_t timer;

        if (ids_queued < 100) {
            // Worker startup is quite expensive. Run the processing directly
            // when only few items need to be processed.
            log_info("Going over {} pending {}s", ids_queued, type);

            for (auto const oid : list) {
                (m_clones[0].get()->*function)(oid);
            }
            m_clones[0]->sync();
        } else {
            log_info("Going over {} pending {}s (using {} threads)", ids_queued,
                     type, m_clones.size());

            std::vector<std::future<void>> workers;
            workers.reserve(m_clones.size() + 1);
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

            if (get_logger().show_progress()) {
                fmt::print(stderr, "\rLeft to process: 0.\n");
            }
        }

        timer.stop();

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

void osmdata_t::process_dependents()
{
    multithreaded_processor proc{m_connection_params, m_mid, m_output,
                                 m_num_procs};

    // stage 1b processing: process parents of changed objects
    if (!m_ways_pending_tracker.empty() || !m_rels_pending_tracker.empty()) {
        if (!m_ways_pending_tracker.empty()) {
            m_ways_pending_tracker.sort_unique();
            proc.process_ways(std::move(m_ways_pending_tracker));
        }
        if (!m_rels_pending_tracker.empty()) {
            m_rels_pending_tracker.sort_unique();
            proc.process_relations(std::move(m_rels_pending_tracker));
        }
        proc.merge_expire_trees();
    }

    // stage 1c processing: mark parent relations of marked objects as changed
    auto const &marked_nodes = m_output->get_marked_node_ids();
    auto const &marked_ways = m_output->get_marked_way_ids();
    if (marked_nodes.empty() && marked_ways.empty()) {
        return;
    }

    // process parent relations of marked nodes and ways
    idlist_t rels_pending_tracker{};
    m_mid->get_node_parents(marked_nodes, nullptr, &rels_pending_tracker);
    m_mid->get_way_parents(marked_ways, &rels_pending_tracker);

    if (rels_pending_tracker.empty()) {
        return;
    }

    rels_pending_tracker.sort_unique();
    proc.process_relations_stage1c(std::move(rels_pending_tracker));
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

void osmdata_t::stop()
{
    if (m_append) {
        process_dependents();
    }

    reprocess_marked();

    postprocess_database();
}
