#ifndef OSM2PGSQL_DB_COPY_HPP
#define OSM2PGSQL_DB_COPY_HPP

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "osmtypes.hpp"
#include "pgsql.hpp"

/**
 * Table information necessary for building SQL queries.
 */
struct db_target_descr_t
{
    /// Schema of the target table (can be empty for default schema)
    std::string schema;
    /// Name of the target table for the copy operation.
    std::string name;
    /// Name of id column used when deleting objects.
    std::string id;
    /// Comma-separated list of rows for copy operation (when empty: all rows)
    std::string rows;

    /**
     * Check if the buffer would use exactly the same copy operation.
     */
    bool same_copy_target(db_target_descr_t const &other) const noexcept
    {
        return (this == &other) || (schema == other.schema &&
                                    name == other.name && rows == other.rows);
    }

    db_target_descr_t() = default;

    db_target_descr_t(std::string n, std::string i, std::string r = {})
    : name(std::move(n)), id(std::move(i)), rows(std::move(r))
    {}
};

/**
 * Deleter which removes objects by id from the database.
 */
class db_deleter_by_id_t
{
    enum
    {
        // There is a trade-off here between sending as few DELETE SQL as
        // possible and keeping the size of the deletable vector managable.
        Max_entries = 1000000
    };

public:
    bool has_data() const noexcept { return !m_deletables.empty(); }

    void add(osmid_t osm_id) { m_deletables.push_back(osm_id); }

    void delete_rows(std::string const &table, std::string const &column,
                     pg_conn_t *conn);

    bool is_full() const noexcept { return m_deletables.size() > Max_entries; }

private:
    /// Vector with object to delete before copying
    std::vector<osmid_t> m_deletables;
};

/**
 * Deleter which removes objects by (optional) type and id from the database.
 */
class db_deleter_by_type_and_id_t
{
    enum
    {
        // There is a trade-off here between sending as few DELETE SQL as
        // possible and keeping the size of the deletable vector managable.
        Max_entries = 1000000
    };

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
                     pg_conn_t *conn);

    bool is_full() const noexcept { return m_deletables.size() > Max_entries; }

private:
    /// Vector with object to delete before copying
    std::vector<item_t> m_deletables;
    bool m_has_type = false;
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
        Cmd_sync, ///< Synchronize with parent.
        Cmd_finish
    };

    virtual ~db_cmd_t() = default;

    cmd_t type;

protected:
    explicit db_cmd_t(cmd_t t) : type(t) {}
};

struct db_cmd_copy_t : public db_cmd_t
{
    enum
    {
        /** Size of a single buffer with COPY data for Postgresql.
         *  This is a trade-off between memory usage and sending large chunks
         *  to speed up processing. Currently a one-size fits all value.
         *  Needs more testing and individual values per queue.
         */
        Max_buf_size = 10 * 1024 * 1024,
        /** Maximum length of the queue with COPY data.
         *  In the usual case, PostgreSQL should be faster processing the
         *  data than it can be produced and there should only be one element
         *  in the queue. If PostgreSQL is slower, then the queue will always
         *  be full and it is better to keep the queue smaller to reduce memory
         *  usage. Current value is just assumed to be a reasonable trade off.
         */
        Max_buffers = 10
    };

    /// Name of the target table for the copy operation
    std::shared_ptr<db_target_descr_t> target;
    /// actual copy buffer
    std::string buffer;

    virtual bool has_deletables() const noexcept = 0;
    virtual void delete_data(pg_conn_t *conn) = 0;

    explicit db_cmd_copy_t(std::shared_ptr<db_target_descr_t> const &t)
    : db_cmd_t(db_cmd_t::Cmd_copy), target(t)
    {
        buffer.reserve(Max_buf_size);
    }
};

template <typename DELETER>
class db_cmd_copy_delete_t : public db_cmd_copy_t
{
public:
    using db_cmd_copy_t::db_cmd_copy_t;

    /// Return true if the buffer is filled up.
    bool is_full() const noexcept
    {
        return (buffer.size() > Max_buf_size - 100) || m_deleter.is_full();
    }

    bool has_deletables() const noexcept override
    {
        return m_deleter.has_data();
    }

    void delete_data(pg_conn_t *conn) override
    {
        if (m_deleter.has_data()) {
            m_deleter.delete_rows(qualified_name(target->schema, target->name),
                                  target->id, conn);
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

struct db_cmd_sync_t : public db_cmd_t
{
    std::promise<void> barrier;

    explicit db_cmd_sync_t(std::promise<void> &&b)
    : db_cmd_t(db_cmd_t::Cmd_sync), barrier(std::move(b))
    {}
};

struct db_cmd_finish_t : public db_cmd_t
{
    db_cmd_finish_t() : db_cmd_t(db_cmd_t::Cmd_finish) {}
};

/**
 * The manager for the worker thread that streams copy data into the database.
 */
class db_copy_thread_t
{
public:
    explicit db_copy_thread_t(std::string const &conninfo);

    db_copy_thread_t(db_copy_thread_t const &) = delete;
    db_copy_thread_t &operator=(db_copy_thread_t const &) = delete;

    db_copy_thread_t(db_copy_thread_t &&) = delete;
    db_copy_thread_t &operator=(db_copy_thread_t &&) = delete;

    ~db_copy_thread_t();

    /**
     * Add another command for the worker.
     */
    void add_buffer(std::unique_ptr<db_cmd_t> &&buffer);

    /**
     * Send sync command and wait for the notification.
     */
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
        std::deque<std::unique_ptr<db_cmd_t>> worker_queue;
    };

    // This is the class that actually instantiated and run in the thread.
    class thread_t
    {
    public:
        thread_t(std::string conninfo, shared &shared);

        void operator()();

    private:
        void write_to_db(db_cmd_copy_t *buffer);
        void start_copy(std::shared_ptr<db_target_descr_t> const &target);
        void finish_copy();
        void delete_rows(db_cmd_copy_t *buffer);

        std::string m_conninfo;
        std::unique_ptr<pg_conn_t> m_conn;

        // Target for copy operation currently ongoing.
        std::shared_ptr<db_target_descr_t> m_inflight;

        // These are shared with the db_copy_thread_t in the main program.
        shared &m_shared;
    };

    std::thread m_worker;

    shared m_shared;
};

#endif // OSM2PGSQL_DB_COPY_HPP
