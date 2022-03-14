#ifndef OSM2PGSQL_THREAD_POOL_HPP
#define OSM2PGSQL_THREAD_POOL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * Contains the class thread_pool_t.
 */

#include "logging.hpp"

#include <osmium/thread/function_wrapper.hpp>
#include <osmium/thread/queue.hpp>

#include <chrono>
#include <future>
#include <thread>
#include <utility>
#include <vector>

/**
 * This stores a future for accessing the result of a task run in the thread
 * pool and the result itself if it has already been obtained. The result
 * is always the run-time of the task.
 */
class task_result_t
{
public:
    /**
     * Initialize this result with the future obtained from the
     * thread_pool_t::submit() function.
     */
    void set(std::future<std::chrono::milliseconds> &&future)
    {
        m_future = std::move(future);
    }

    /**
     * Wait for the task to finish.
     *
     * \return The runtime of the task.
     * \throws Any exception the task has thrown.
     */
    std::chrono::milliseconds wait();

    /**
     * Return the run-time of this task. Will be 0 if the task has not
     * yet finished or >0 if the task has finished.
     */
    std::chrono::milliseconds runtime() const noexcept { return m_result; }

private:
    std::future<std::chrono::milliseconds> m_future{};
    std::chrono::milliseconds m_result{};
}; // class task_result_t

/**
 * This is a thread pool class. You can submit tasks using the submit()
 * function. Tasks can only return void.
 */
class thread_pool_t
{
public:
    /**
     * Create thread pool with the specified number of threads.
     */
    explicit thread_pool_t(unsigned int num_threads);

    void shutdown_all_workers();

    thread_pool_t(thread_pool_t const &) = delete;
    thread_pool_t &operator=(thread_pool_t const &) = delete;

    thread_pool_t(thread_pool_t &&) = delete;
    thread_pool_t &operator=(thread_pool_t &&) = delete;

    ~thread_pool_t() { shutdown_all_workers(); }

    /**
     * Submit a function to the thread pool. The task is queued and will
     * run when a thread is available.
     *
     * \param func The function to be run as a task in the thread pool.
     * \return A future with which the task can be waited for and the run-time
     *         of the task can be queried.
     */
    template <typename TFunction>
    std::future<std::chrono::milliseconds> submit(TFunction &&func)
    {
        std::packaged_task<std::chrono::milliseconds()> task{
            [f = std::forward<TFunction>(func)]() {
                log_debug("Starting task...");
                auto const start_time = std::chrono::steady_clock::now();
                f();
                auto const end_time = std::chrono::steady_clock::now();
                auto const run_time =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time);

                log_debug("Done task in {}.", run_time);

                return run_time;
            }};
        std::future<std::chrono::milliseconds> future_result{task.get_future()};
        m_work_queue.push(std::move(task));

        return future_result;
    }

    /// Return the number of threads in this thread pool.
    std::size_t num_threads() const noexcept { return m_threads.size(); }

private:
    /**
     * This class makes sure all pool threads will be joined when
     * the pool is destructed.
     */
    class thread_joiner
    {

        std::vector<std::thread> *m_threads;

    public:
        explicit thread_joiner(std::vector<std::thread> *threads)
        : m_threads(threads)
        {}

        thread_joiner(thread_joiner const &) = delete;
        thread_joiner &operator=(thread_joiner const &) = delete;

        thread_joiner(thread_joiner &&) = delete;
        thread_joiner &operator=(thread_joiner &&) = delete;

        ~thread_joiner()
        {
            for (auto &thread : *m_threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
        }

    }; // class thread_joiner

    static constexpr std::size_t const max_queue_size = 32;

    osmium::thread::Queue<osmium::thread::function_wrapper> m_work_queue;
    std::vector<std::thread> m_threads;
    thread_joiner m_joiner;

    /**
     * This is the function run in each worker thread. It will loop over
     * all tasks in finds in the work queue until it encounters a special
     * "empty" tasks at which point it will return ending the thread.
     */
    void worker_thread(unsigned int thread_num);

}; // class thread_pool_t

#endif // OSM2PGSQL_THREAD_POOL_HPP
