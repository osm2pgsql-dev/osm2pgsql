#include <cassert>

#include "db-copy.hpp"
#include "format.hpp"
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

    conn->exec(fmt::to_string(sql));
}

db_copy_thread_t::db_copy_thread_t(std::string const &conninfo)
{
    // conninfo is captured by copy here, because we don't know wether the
    // reference will still be valid once we get around to running the thread
    m_worker = std::thread(thread_t{conninfo, m_shared});
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
        std::unique_ptr<db_cmd_t>(new db_cmd_sync_t(std::move(barrier))));
    sync.wait();
}

void db_copy_thread_t::finish()
{
    if (m_worker.joinable()) {
        add_buffer(std::unique_ptr<db_cmd_t>(new db_cmd_finish_t()));
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
        m_conn->exec("SET synchronous_commit TO off");

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
        fmt::print(stderr, "DB copy thread failed: {}\n", e.what());
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

    std::string copystr = "COPY ";
    copystr.reserve(target->name.size() + target->rows.size() + 14);
    copystr += target->name;
    if (!target->rows.empty()) {
        copystr += '(';
        copystr += target->rows;
        copystr += ')';
    }
    copystr += " FROM STDIN";
    m_conn->query(PGRES_COPY_IN, copystr);

    m_inflight = target;
}

void db_copy_thread_t::thread_t::finish_copy()
{
    if (m_inflight) {
        m_conn->end_copy(m_inflight->name);
        m_inflight.reset();
    }
}
