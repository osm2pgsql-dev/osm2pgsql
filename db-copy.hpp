#ifndef DB_COPY_HPP
#define DB_COPY_HPP

#include <condition_variable>
#include <deque>
#include <future>
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

    db_target_descr_t() = default;
    db_target_descr_t(char const *n, char const *i, char const *r = "")
    : name(n), rows(r), id(i)
    {
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
    void worker_thread();

    void connect();
    void disconnect();

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

    template <typename T, typename ...ARGS>
    void add_columns(T value, ARGS&&... args)
    {
        add_column(value);
        add_columns(args...);
    }

    template <typename T>
    void add_columns(T value)
    {
        add_column(value);
    }

    /// Add a column entry of simple type.
    template <typename T>
    void add_column(T value)
    {
        add_value(value);
        m_current->buffer += '\t';
    }

    /// Add an empty column.
    void add_null_column() { m_current->buffer += "\\N\t"; }

    /// Start an array column.
    void new_array() { m_current->buffer += "{"; }

    /// Add a single value to an array column
    template <typename T>
    void add_array_elem(T value)
    {
        add_value(value);
        m_current->buffer += ',';
    }

    void add_array_elem(std::string const &s) { add_array_elem(s.c_str()); }

    void add_array_elem(char const *s)
    {
        assert(m_current);
        m_current->buffer += '"';
        add_escaped_string(s);
        m_current->buffer += "\",";
    }

    /// Finish an array column.
    void finish_array()
    {
        auto idx = m_current->buffer.size() - 1;
        if (m_current->buffer[idx] == '{')
            m_current->buffer += '}';
        else
            m_current->buffer[idx] = '}';
        m_current->buffer += '\t';
    }

    /// Start a hash column.
    void new_hash() { /* nothing */}

    void add_hash_elem(std::string const &k, std::string const &v)
    {
        add_hash_elem(k.c_str(), v.c_str());
    }

    void add_hash_elem(char const *k, char const *v)
    {
        m_current->buffer += '"';
        add_escaped_string(k);
        m_current->buffer += "\"=>\"";
        add_escaped_string(v);
        m_current->buffer += "\",";
    }

    void finish_hash()
    {
        auto idx = m_current->buffer.size() - 1;
        if (!m_current->buffer.empty() && m_current->buffer[idx] == ',') {
            m_current->buffer[idx] = '\t';
        } else {
            m_current->buffer += '\t';
        }
    }

    void add_hex_geom(std::string const &wkb)
    {
        char const *lookup_hex = "0123456789ABCDEF";

        for (char c : wkb) {
            m_current->buffer += lookup_hex[(c >> 4) & 0xf];
            m_current->buffer += lookup_hex[c & 0xf];
        }
        m_current->buffer += '\t';
    }

    /**
     * Mark an OSM object for deletion in the current table.
     *
     * The object is guaranteed to be deleted before any lines
     * following the delete_id() are inserted.
     */
    void delete_id(osmid_t osm_id);

    /**
     * Synchronize with worker.
     *
     * Only returns when all previously issued commands are done.
     */
    void sync();

private:
    template <typename T>
    void add_value(T value)
    {
        m_current->buffer += std::to_string(value);
    }

    void add_value(double value)
    {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%g", value);
        m_current->buffer += tmp;
    }

    void add_value(std::string const &s) { add_value(s.c_str()); }

    void add_value(char const *s)
    {
        assert(m_current);
        for (char const *c = s; *c; ++c) {
            switch (*c) {
                case '"':
                    m_current->buffer += "\\\"";
                    break;
                case '\\':
                    m_current->buffer += "\\\\";
                    break;
                case '\n':
                    m_current->buffer += "\\n";
                    break;
                case '\r':
                    m_current->buffer += "\\r";
                    break;
                case '\t':
                    m_current->buffer += "\\t";
                    break;
                default:
                    m_current->buffer += *c;
                    break;
            }
        }
    }

    void add_escaped_string(char const *s)
    {
        for (char const *c = s; *c; ++c) {
            switch (*c) {
            case '"':
                m_current->buffer += "\\\\\"";
                break;
            case '\\':
                m_current->buffer += "\\\\\\\\";
                break;
            case '\n':
                m_current->buffer += "\\n";
                break;
            case '\r':
                m_current->buffer += "\\r";
                break;
            case '\t':
                m_current->buffer += "\\t";
                break;
            default:
                m_current->buffer += *c;
                break;
            }
        }
    }

    std::shared_ptr<db_copy_thread_t> m_processor;
    std::unique_ptr<db_cmd_copy_t> m_current;
    unsigned m_last_line;
};

#endif
