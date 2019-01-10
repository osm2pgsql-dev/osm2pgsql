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
    /// If empty, then `buffer` contains a single SQL command to execute.
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
 * Buffer containing stuff to copy into the database.
 */
struct db_copy_buffer_t
{
    /// Name of the target table for the copy operation
    std::shared_ptr<db_target_descr_t> target;
    /// Vector with object to delete before copying
    std::vector<osmid_t> deletables;
    /// actual copy buffer
    std::string buffer;

    explicit db_copy_buffer_t(std::shared_ptr<db_target_descr_t> const &t)
    : target(t)
    {}

    explicit db_copy_buffer_t(std::string const &sql_command)
    : buffer(sql_command)
    {
    }

    bool is_copy_buffer() const { return !!target; }
};

/**
 * The worker thread that streams copy data into the database.
 */
class db_copy_thread_t
{
public:
    db_copy_thread_t(std::string const &conninfo);

    /**
     * Add another buffer for copying into the database.
     */
    void add_buffer(std::unique_ptr<db_copy_buffer_t> &&buffer);

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
    void commit();

    void execute_sql(std::string const &sql_cmd);
    void write_to_db(std::unique_ptr<db_copy_buffer_t> &&buffer);
    void start_copy(std::unique_ptr<db_copy_buffer_t> &&buffer);
    void finish_copy();
    void delete_rows(db_copy_buffer_t *buffer);

    std::string m_conninfo;
    pg_conn *m_conn;

    std::thread m_worker;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cond;
    std::deque<std::unique_ptr<db_copy_buffer_t>> m_worker_queue;

    std::unique_ptr<db_copy_buffer_t> m_inflight;
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

private:
    std::shared_ptr<db_copy_thread_t> m_processor;
    std::unique_ptr<db_copy_buffer_t> m_current;
    unsigned m_last_line;
};

#endif
