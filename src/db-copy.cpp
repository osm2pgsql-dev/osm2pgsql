#include <cassert>

#include "db-copy.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"

void db_deleter_by_id_t::delete_rows(std::string const &table,
                                     std::string const &column, pg_conn_t *conn)
{
    fmt::memory_buffer sql;
    // Each deletable contributes an OSM ID and a comma. The highest node ID
    // currently has 10 digits, so 15 characters should do for a couple of years.
    // Add 50 characters for the SQL statement itself.
    sql.reserve(m_deletables.size() * 15 + 50);

    fmt::format_to(sql, FMT_STRING("DELETE FROM {} WHERE {} IN ("), table,
                   column);

    for (auto id : m_deletables) {
        format_to(sql, FMT_STRING("{},"), id);
    }
    sql[sql.size() - 1] = ')';

    sql.push_back('\0');
    conn->exec(sql.data());
}

void db_deleter_by_type_and_id_t::delete_rows(std::string const &table,
                                              std::string const &column,
                                              pg_conn_t *conn)
{
    assert(!m_deletables.empty());

    fmt::memory_buffer sql;
    // Need a VALUES line for each deletable: type (3 bytes), id (15 bytes),
    // braces etc. (4 bytes). And additional space for the remainder of the
    // SQL command.
    sql.reserve(m_deletables.size() * 22 + 200);

    if (m_has_type) {
        fmt::format_to(sql, "DELETE FROM {} p USING (VALUES ", table);

        for (auto const &item : m_deletables) {
            fmt::format_to(sql, FMT_STRING("('{}',{}),"), item.osm_type,
                           item.osm_id);
        }

        sql.resize(sql.size() - 1);

        auto const pos = column.find(',');
        assert(pos != std::string::npos);
        std::string type = column.substr(0, pos);

        fmt::format_to(sql,
                       ") AS t (osm_type, osm_id) WHERE"
                       " p.{} = t.osm_type AND p.{} = t.osm_id",
                       type, column.c_str() + pos + 1);
    } else {
        fmt::format_to(sql, FMT_STRING("DELETE FROM {} WHERE {} IN ("), table,
                       column);

        for (auto const &item : m_deletables) {
            format_to(sql, FMT_STRING("{},"), item.osm_id);
        }
        sql[sql.size() - 1] = ')';
    }

    sql.push_back('\0');
    conn->exec(sql.data());
}

db_copy_thread_t::db_copy_thread_t(std::string const &conninfo)
{
    // conninfo is captured by copy here, because we don't know wether the
    // reference will still be valid once we get around to running the thread
    m_worker = std::thread{thread_t{conninfo, m_shared}};
}

db_copy_thread_t::~db_copy_thread_t() { finish(); }

void db_copy_thread_t::add_buffer(std::unique_ptr<db_cmd_t> &&buffer)
{
    assert(m_worker.joinable()); // thread must not have been finished

    std::unique_lock<std::mutex> lock{m_shared.queue_mutex};
    m_shared.queue_full_cond.wait(lock, [&] {
        return m_shared.worker_queue.size() < db_cmd_copy_t::Max_buffers;
    });

    m_shared.worker_queue.push_back(std::move(buffer));
    m_shared.queue_cond.notify_one();
}

void db_copy_thread_t::sync_and_wait()
{
    std::promise<void> barrier;
    std::future<void> sync = barrier.get_future();
    add_buffer(
        std::unique_ptr<db_cmd_t>(new db_cmd_sync_t{std::move(barrier)}));
    sync.wait();
}

void db_copy_thread_t::finish()
{
    if (m_worker.joinable()) {
        add_buffer(std::unique_ptr<db_cmd_t>(new db_cmd_finish_t{}));
        m_worker.join();
    }
}

db_copy_thread_t::thread_t::thread_t(std::string conninfo, shared &shared)
: m_conninfo(std::move(conninfo)), m_shared(shared)
{}

void db_copy_thread_t::thread_t::operator()()
{
    try {
        m_conn.reset(new pg_conn_t{m_conninfo});

        // Let commits happen faster by delaying when they actually occur.
        m_conn->exec("SET synchronous_commit = off");

        bool done = false;
        while (!done) {
            std::unique_ptr<db_cmd_t> item;
            {
                std::unique_lock<std::mutex> lock{m_shared.queue_mutex};
                m_shared.queue_cond.wait(
                    lock, [&] { return !m_shared.worker_queue.empty(); });

                item = std::move(m_shared.worker_queue.front());
                m_shared.worker_queue.pop_front();
                m_shared.queue_full_cond.notify_one();
            }

            switch (item->type) {
            case db_cmd_t::Cmd_copy:
                write_to_db(static_cast<db_cmd_copy_t *>(item.get()));
                break;
            case db_cmd_t::Cmd_sync:
                finish_copy();
                static_cast<db_cmd_sync_t *>(item.get())->barrier.set_value();
                break;
            case db_cmd_t::Cmd_finish:
                done = true;
                break;
            }
        }

        finish_copy();

        m_conn.reset();
    } catch (std::runtime_error const &e) {
        log_error("DB copy thread failed: {}", e.what());
        exit(2);
    }
}

void db_copy_thread_t::thread_t::write_to_db(db_cmd_copy_t *buffer)
{
    if (buffer->has_deletables() ||
        (m_inflight && !buffer->target->same_copy_target(*m_inflight.get()))) {
        finish_copy();
    }

    buffer->delete_data(m_conn.get());

    if (!m_inflight) {
        start_copy(buffer->target);
    }

    m_conn->copy_data(buffer->buffer, buffer->target->name);
}

void db_copy_thread_t::thread_t::start_copy(
    std::shared_ptr<db_target_descr_t> const &target)
{
    assert(!m_inflight);

    auto const qname = qualified_name(target->schema, target->name);
    fmt::memory_buffer sql;
    sql.reserve(qname.size() + target->rows.size() + 20);
    if (target->rows.empty()) {
        fmt::format_to(sql, FMT_STRING("COPY {} FROM STDIN"), qname);
    } else {
        fmt::format_to(sql, FMT_STRING("COPY {} ({}) FROM STDIN"), qname,
                       target->rows);
    }

    sql.push_back('\0');
    m_conn->query(PGRES_COPY_IN, sql.data());

    m_inflight = target;
}

void db_copy_thread_t::thread_t::finish_copy()
{
    if (m_inflight) {
        m_conn->end_copy(m_inflight->name);
        m_inflight.reset();
    }
}
