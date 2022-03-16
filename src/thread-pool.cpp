/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "thread-pool.hpp"

#include <osmium/thread/util.hpp>

#include <cassert>
#include <string>

std::chrono::milliseconds task_result_t::wait()
{
    if (m_future.valid()) {
        m_result = m_future.get();

        // Make sure the result is not 0 so it is different than
        // "no result yet".
        if (m_result == std::chrono::milliseconds::zero()) {
            ++m_result;
        }
    }
    return m_result;
}

thread_pool_t::thread_pool_t(unsigned int num_threads)
: m_work_queue(max_queue_size, "work"), m_joiner(&m_threads)
{
    assert(num_threads > 0);
    try {
        for (unsigned int n = 0; n < num_threads; ++n) {
            m_threads.emplace_back(&thread_pool_t::worker_thread, this, n);
        }
    } catch (...) {
        shutdown_all_workers();
        throw;
    }
}

void thread_pool_t::shutdown_all_workers()
{
    for (std::size_t n = 0; n < m_threads.size(); ++n) {
        // The special function wrapper makes a worker shut down.
        m_work_queue.push(osmium::thread::function_wrapper{0});
    }
}

void thread_pool_t::worker_thread(unsigned int thread_num)
{
    std::string name{"_osm2pgsql_worker_"};
    name.append(std::to_string(thread_num));
    osmium::thread::set_thread_name(name.c_str());
    this_thread_num = thread_num + 1;

    while (true) {
        osmium::thread::function_wrapper task;
        m_work_queue.wait_and_pop(task);
        if (task && task()) {
            // The called tasks returns true only when the
            // worker thread should shut down.
            return;
        }
    }
}
