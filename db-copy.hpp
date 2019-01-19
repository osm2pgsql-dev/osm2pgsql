#ifndef DB_COPY_HPP
#define DB_COPY_HPP

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "osmtypes.hpp"

struct pg_conn;

/**
 * Table information necessary for building SQL queries.
 */
struct db_target_descr_t
{
    /// Name of the target table for the copy operation.
    std::string name;
    /// Comma-separated list of rows for copy operation (when empty: all rows)
    std::string rows;
    /// Name of id column used when deleting objects.
    std::string id;

    /**
     * Check if the buffer would use exactly the same copy operation.
     */
    bool same_copy_target(db_target_descr_t const &other) const noexcept
    {
        return (this == &other) || (name == other.name && rows == other.rows);
    }
};

/**
 * A command for the copy thread to execute.
 */
class db_cmd_t
{
public:
    enum cmd_t
    {
        Cmd_copy, ///< Copy buffer content into given target.
        Cmd_sql,  ///< Execute buffer content as SQL command.
        Cmd_sync, ///< Synchronize with parent.
        Cmd_finish
    };

    cmd_t type;

protected:
    explicit db_cmd_t(cmd_t t)
    : type(t)
    {
    }
};

struct db_cmd_copy_t : public db_cmd_t
{
    enum { Max_buf_size = 10 * 1024 * 1024 };
    /// Name of the target table for the copy operation
    std::shared_ptr<db_target_descr_t> target;
    /// Vector with object to delete before copying
    std::vector<osmid_t> deletables;
    /// actual copy buffer
    std::string buffer;

    explicit db_cmd_copy_t(std::shared_ptr<db_target_descr_t> const &t)
    : db_cmd_t(db_cmd_t::Cmd_copy), target(t)
    {
        buffer.reserve(Max_buf_size);
    }
};

struct db_cmd_sql_t : public db_cmd_t
{
    /// actual copy buffer
    std::string buffer;

    explicit db_cmd_sql_t(std::string const &sql_command)
    : db_cmd_t(db_cmd_t::Cmd_sql), buffer(sql_command)
    {
    }

};

struct db_cmd_sync_t : public db_cmd_t
{
    std::promise<void> barrier;

    explicit db_cmd_sync_t(std::promise<void> &&b)
    : db_cmd_t(db_cmd_t::Cmd_sync), barrier(std::move(b))
    {
    }
};

struct db_cmd_finish_t : public db_cmd_t
{
    db_cmd_finish_t() : db_cmd_t(db_cmd_t::Cmd_finish) {}
};

/**
 * The worker thread that streams copy data into the database.
 */
class db_copy_thread_t
{
public:
    db_copy_thread_t(std::string const &conninfo);

    /**
     * Add another command for the worker.
     */
    void add_buffer(std::unique_ptr<db_cmd_t> &&buffer);

    /**
     * Finish the copy process.
     *
     * Only returns when all remaining data has been committed to the
     * database.
     */
    void finish();

private:
    void worker_thread();

    void connect();
    void disconnect();

    void execute_sql(std::string const &sql_cmd);
    void write_to_db(db_cmd_copy_t *buffer);
    void start_copy(std::shared_ptr<db_target_descr_t> const &target);
    void finish_copy();
    void delete_rows(db_cmd_copy_t *buffer);

    std::string m_conninfo;
    pg_conn *m_conn;

    std::thread m_worker;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cond;
    std::deque<std::unique_ptr<db_cmd_t>> m_worker_queue;

    // Target for copy operation currently ongoing.
    std::shared_ptr<db_target_descr_t> m_inflight;
};

/**
 * Management class that fills and manages copy buffers.
 */
class db_copy_mgr_t
{
public:
    db_copy_mgr_t(std::shared_ptr<db_copy_thread_t> const &processor);

    /**
     * Start a new table row.
     */
    void new_line(std::shared_ptr<db_target_descr_t> const &table);

    /**
     * Finish a table row.
     *
     * Adds the row delimiter to the buffer.
     */
    void finish_line();

    /**
     * Add a column entry of simple type.
     */
    template <typename T>
    void add_column(T value)
    {
        auto s = std::to_string(value);
        add_column<>(s);
    }

    template <>
    void add_column(std::string const &s)
    {
        assert(m_current);
        m_current->buffer += s;
    }

    /**
     * Mark an OSM object for deletion in the current table.
     *
     * The object is guaranteed to be deleted before any lines
     * following the delete_id() are inserted.
     */
    void delete_id(osmid_t osm_id);

    /**
     * Run an SQL statement.
     *
     * The statement is run in order. That means any previously submitted
     * copyblocks are finished first.
     */
    void exec_sql(std::string const &sql_cmd);

    /**
     * Synchronize with worker.
     *
     * Only returns when all previously issued commands are done.
     */
    void sync();

private:
    std::shared_ptr<db_copy_thread_t> m_processor;
    std::unique_ptr<db_cmd_copy_t> m_current;
    unsigned m_last_line;
};

#endif
