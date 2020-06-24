#ifndef OSM2PGSQL_THREAD_POOL_HPP
#define OSM2PGSQL_THREAD_POOL_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * Contains the class thread_pool_t.
 */

#include <osmium/thread/pool.hpp>

#include <future>
#include <utility>
#include <vector>

/**
 * This is a simple thread pool class. You can submit tasks using the submit()
 * function. Tasks can only return void. Use the check_for_exceptions() function
 * before destructing the pool to make sure all functions finished without
 * throwing an exception.
 */
class thread_pool_t
{
public:
    explicit thread_pool_t(int num_threads) : m_pool(num_threads, 512) {}

    template <typename FUNC>
    void submit(FUNC &&func)
    {
        m_results.push_back(m_pool.submit(std::forward<FUNC>(func)));
    }

    // This will throw if any of the tasks run in the thread pool did throw
    // an exception.
    void check_for_exceptions()
    {
        for (auto &&result : m_results) {
            result.get();
        }
        m_results.clear();
    }

private:
    osmium::thread::Pool m_pool;
    std::vector<std::future<void>> m_results;
};

#endif // OSM2PGSQL_THREAD_POOL_HPP
