#ifndef OSM2PGSQL_DB_COPY_HPP
#define OSM2PGSQL_DB_COPY_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "pgsql-params.hpp"

#include <cassert>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

/**
 * Table information necessary for building SQL queries.
 */
class db_target_descr_t
{
public:
    db_target_descr_t(std::string schema, std::string name, std::string id,
                      std::string rows = {})
    : m_schema(std::move(schema)), m_name(std::move(name)), m_id(std::move(id)),
      m_rows(std::move(rows))
    {
        assert(!m_schema.empty());
        assert(!m_name.empty());
    }

    std::string const &schema() const noexcept { return m_schema; }
    std::string const &name() const noexcept { return m_name; }
    std::string const &id() const noexcept { return m_id; }
    std::string const &rows() const noexcept { return m_rows; }

    void set_rows(std::string rows) { m_rows = std::move(rows); }

    /**
     * Check if the buffer would use exactly the same copy operation.
     */
    bool same_copy_target(db_target_descr_t const &other) const noexcept
    {
        return (this == &other) ||
               (m_schema == other.m_schema && m_name == other.m_name &&
                m_id == other.m_id && m_rows == other.m_rows);
    }

private:
    /// Schema of the target table.
    std::string m_schema;
    /// Name of the target table for the copy operation.
    std::string m_name;
    /// Name of id column used when deleting objects.
    std::string m_id;
    /// Comma-separated list of rows for copy operation (when empty: all rows)
    std::string m_rows;
};

/**
 * Deleter which removes objects by id from the database.
 */
class db_deleter_by_id_t
{
    /**
     * There is a trade-off here between sending as few DELETE SQL as
     * possible and keeping the size of the deletable vector managable.
     */
    static constexpr std::size_t MAX_ENTRIES = 1000000;

public:
    bool has_data() const noexcept { return !m_deletables.empty(); }

    void add(osmid_t osm_id) { m_deletables.push_back(osm_id); }

    void delete_rows(std::string const &table, std::string const &column,
                     pg_conn_t const &db_connection);

    bool is_full() const noexcept { return m_deletables.size() > MAX_ENTRIES; }

private:
    /// Vector with object to delete before copying
    std::vector<osmid_t> m_deletables;
};

/**
 * Deleter which removes objects by (optional) type and id from the database.
 */
class db_deleter_by_type_and_id_t
{
    /**
     * There is a trade-off here between sending as few DELETE SQL as
     * possible and keeping the size of the deletable vector managable.
     */
    static constexpr std::size_t MAX_ENTRIES = 1000000;

    struct item_t
    {
        osmid_t osm_id;
        char osm_type;

        item_t(char t, osmid_t i) : osm_id(i), osm_type(t) {}
    };

public:
    bool has_data() const noexcept { return !m_deletables.empty(); }

    void add(char type, osmid_t osm_id)
    {
        m_deletables.emplace_back(type, osm_id);
        if (type != 'X') {
            m_has_type = true;
        }
    }

    void delete_rows(std::string const &table, std::string const &column,
                     pg_conn_t const &db_connection);

    bool is_full() const noexcept { return m_deletables.size() > MAX_ENTRIES; }

private:
    /// Vector with object to delete before copying
    std::vector<item_t> m_deletables;
    bool m_has_type = false;
};

struct db_cmd_copy_t
{
    /**
     * Size of a single buffer with COPY data for Postgresql.
     * This is a trade-off between memory usage and sending large chunks
     * to speed up processing. Currently a one-size fits all value.
     * Needs more testing and individual values per queue.
     */
    static constexpr std::size_t MAX_BUF_SIZE = 10UL * 1024UL * 1024UL;

    /**
     * Maximum length of the queue with COPY data.
     * In the usual case, PostgreSQL should be faster processing the
     * data than it can be produced and there should only be one element
     * in the queue. If PostgreSQL is slower, then the queue will always
     * be full and it is better to keep the queue smaller to reduce memory
     * usage. Current value is just assumed to be a reasonable trade off.
     */
    static constexpr std::size_t MAX_BUFFERS = 10;

    /// Name of the target table for the copy operation
    std::shared_ptr<db_target_descr_t> target;
    /// actual copy buffer
    std::string buffer;

    db_cmd_copy_t() = default;

    explicit db_cmd_copy_t(std::shared_ptr<db_target_descr_t> t)
    : target(std::move(t))
    {
        buffer.reserve(MAX_BUF_SIZE);
    }

    explicit operator bool() const noexcept { return target != nullptr; }
};

template <typename DELETER>
class db_cmd_copy_delete_t : public db_cmd_copy_t
{
public:
    using db_cmd_copy_t::db_cmd_copy_t;

    /// Return true if the buffer is filled up.
    bool is_full() const noexcept
    {
        return (buffer.size() > MAX_BUF_SIZE - 100) || m_deleter.is_full();
    }

    bool has_deletables() const noexcept { return m_deleter.has_data(); }

    void delete_data(pg_conn_t const &db_connection)
    {
        if (m_deleter.has_data()) {
            m_deleter.delete_rows(
                qualified_name(target->schema(), target->name()), target->id(),
                db_connection);
        }
    }

    template <typename... ARGS>
    void add_deletable(ARGS &&... args)
    {
        m_deleter.add(std::forward<ARGS>(args)...);
    }

private:
    /// Deleter class for old items
    DELETER m_deleter;
};

struct db_cmd_end_copy_t
{
};

struct db_cmd_sync_t
{
    std::promise<void> barrier;

    explicit db_cmd_sync_t(std::promise<void> &&b) : barrier(std::move(b)) {}
};

struct db_cmd_finish_t
{
};

/**
 * This type implements the commands that can be sent through the worker
 * queue to the worker thread.
 */
using db_cmd_t =
    std::variant<db_cmd_copy_delete_t<db_deleter_by_id_t>,
                 db_cmd_copy_delete_t<db_deleter_by_type_and_id_t>,
                 db_cmd_end_copy_t, db_cmd_sync_t, db_cmd_finish_t>;

/**
 * The manager for the worker thread that streams copy data into the database.
 */
class db_copy_thread_t
{
public:
    explicit db_copy_thread_t(connection_params_t const &connection_params);

    db_copy_thread_t(db_copy_thread_t const &) = delete;
    db_copy_thread_t &operator=(db_copy_thread_t const &) = delete;

    db_copy_thread_t(db_copy_thread_t &&) = delete;
    db_copy_thread_t &operator=(db_copy_thread_t &&) = delete;

    ~db_copy_thread_t();

    /// Add a command to the worker queue.
    void send_command(db_cmd_t &&buffer);

    /// Close COPY if one is open.
    void end_copy();

    /// Send sync command and wait for it to finish.
    void sync_and_wait();

    /**
     * Finish the copy process.
     *
     * Only returns when all remaining data has been committed to the
     * database.
     */
    void finish();

private:
    struct shared
    {
        std::mutex queue_mutex;
        std::condition_variable queue_cond;
        std::condition_variable queue_full_cond;
        std::deque<db_cmd_t> worker_queue;
    };

    // This is the class that actually instantiated and run in the thread.
    class thread_t
    {
    public:
        thread_t(pg_conn_t &&db_connection, shared *shared);

        void operator()();

    private:
        template <typename DELETER>
        bool execute(db_cmd_copy_delete_t<DELETER> &cmd);

        bool execute(db_cmd_end_copy_t &);

        bool execute(db_cmd_sync_t &cmd);

        static bool execute(db_cmd_finish_t &) { return true; }

        void start_copy(std::shared_ptr<db_target_descr_t> const &target);
        void finish_copy();
        void delete_rows(db_cmd_copy_t *buffer);

        connection_params_t m_connection_params;
        pg_conn_t m_db_connection;

        // Target for copy operation currently ongoing.
        std::shared_ptr<db_target_descr_t> m_inflight;

        // These are shared with the db_copy_thread_t in the main program.
        shared *m_shared;
    };

    std::thread m_worker;

    shared m_shared;
};

#endif // OSM2PGSQL_DB_COPY_HPP
