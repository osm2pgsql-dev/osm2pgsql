#include <boost/format.hpp>
#include <cassert>
#include <cstdio>
#include <future>
#include <thread>

#include "db-copy.hpp"
#include "pgsql.hpp"

using fmt = boost::format;

db_copy_thread_t::db_copy_thread_t(std::string const &conninfo)
: m_conninfo(conninfo), m_conn(nullptr)
{
    m_worker = std::thread([this]() {
        try {
            worker_thread();
        } catch (std::runtime_error const &e) {
            fprintf(stderr, "DB writer thread failed due to ERROR: %s\n",
                    e.what());
            exit(2);
        }
    });
}

db_copy_thread_t::~db_copy_thread_t() { finish(); }

void db_copy_thread_t::add_buffer(std::unique_ptr<db_cmd_t> &&buffer)
{
    assert(m_worker.joinable()); // thread must not have been finished
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    m_worker_queue.push_back(std::move(buffer));
    m_queue_cond.notify_one();
}

void db_copy_thread_t::sync_and_wait()
{
    std::promise<void> barrier;
    std::future<void> sync = barrier.get_future();
    add_buffer(std::unique_ptr<db_cmd_t>(new db_cmd_sync_t(std::move(barrier))));
    sync.wait();
}

void db_copy_thread_t::finish()
{
    if (m_worker.joinable()) {
        finish_copy();

        add_buffer(std::unique_ptr<db_cmd_t>(new db_cmd_finish_t()));
        m_worker.join();
    }
}

void db_copy_thread_t::worker_thread()
{
    connect();

    bool done = false;
    while (!done) {
        std::unique_ptr<db_cmd_t> item;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            if (m_worker_queue.empty()) {
                m_queue_cond.wait(lock);
                continue;
            }

            item = std::move(m_worker_queue.front());
            m_worker_queue.pop_front();
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

    disconnect();
}

void db_copy_thread_t::connect()
{
    assert(!m_conn);

    PGconn *conn = PQconnectdb(m_conninfo.c_str());
    if (PQstatus(conn) != CONNECTION_OK)
        throw std::runtime_error(
            (fmt("Connection to database failed: %1%\n") % PQerrorMessage(conn))
                .str());
    m_conn = conn;

    // Let commits happen faster by delaying when they actually occur.
    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK,
                      "SET synchronous_commit TO off;");
}

void db_copy_thread_t::disconnect()
{
    if (!m_conn)
        return;

    PQfinish(m_conn);
    m_conn = nullptr;
}

void db_copy_thread_t::write_to_db(db_cmd_copy_t *buffer)
{
    if (!buffer->deletables.empty() ||
        (m_inflight && !buffer->target->same_copy_target(*m_inflight.get())))
        finish_copy();

    if (!buffer->deletables.empty())
        delete_rows(buffer);

    if (!m_inflight)
        start_copy(buffer->target);

    pgsql_CopyData(buffer->target->name.c_str(), m_conn, buffer->buffer);
}

void db_copy_thread_t::delete_rows(db_cmd_copy_t *buffer)
{
    assert(!m_inflight);

    std::string sql = "DELETE FROM ";
    sql.reserve(buffer->target->name.size() + buffer->deletables.size() * 15 +
                30);
    sql += buffer->target->name;
    sql += " WHERE ";
    sql += buffer->target->id;
    sql += " IN (";
    for (auto id : buffer->deletables) {
        sql += std::to_string(id);
        sql += ',';
    }
    sql[sql.size() - 1] = ')';

    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, sql);
}

void db_copy_thread_t::start_copy(std::shared_ptr<db_target_descr_t> const &target)
{
    m_inflight = target;

    std::string copystr = "COPY ";
    copystr.reserve(target->name.size() + target->rows.size() + 14);
    copystr += target->name;
    if (!target->rows.empty()) {
        copystr += '(';
        copystr += target->rows;
        copystr += ')';
    }
    copystr += " FROM STDIN";
    pgsql_exec_simple(m_conn, PGRES_COPY_IN, copystr);

    m_inflight = target;
}

void db_copy_thread_t::finish_copy()
{
    if (!m_inflight)
        return;

    if (PQputCopyEnd(m_conn, nullptr) != 1)
        throw std::runtime_error((fmt("stop COPY_END for %1% failed: %2%\n") %
                                  m_inflight->name %
                                  PQerrorMessage(m_conn))
                                     .str());

    pg_result_t res(PQgetResult(m_conn));
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
        throw std::runtime_error((fmt("result COPY_END for %1% failed: %2%\n") %
                                  m_inflight->name %
                                  PQerrorMessage(m_conn))
                                     .str());

    m_inflight.reset();
}

db_copy_mgr_t::db_copy_mgr_t(std::shared_ptr<db_copy_thread_t> const &processor)
: m_processor(processor)
{}

void db_copy_mgr_t::new_line(std::shared_ptr<db_target_descr_t> const &table)
{
    if (!m_current || !m_current->target->same_copy_target(*table.get())) {
        if (m_current) {
            m_processor->add_buffer(std::move(m_current));
        }

        m_current.reset(new db_cmd_copy_t(table));
    }
}

void db_copy_mgr_t::delete_id(osmid_t osm_id)
{
    assert(m_current);
    m_current->deletables.push_back(osm_id);
}

void db_copy_mgr_t::sync()
{
    // finish any ongoing copy operations
    if (m_current) {
        m_processor->add_buffer(std::move(m_current));
    }

    m_processor->sync_and_wait();
}

void db_copy_mgr_t::finish_line()
{
    assert(m_current);

    auto &buf = m_current->buffer;
    assert(!buf.empty());

    // Expect that a column has been written last which ended in a '\t'.
    // Replace it with the row delimiter '\n'.
    auto sz = buf.size();
    assert(buf[sz - 1] == '\t');
    buf[sz - 1] = '\n';

    if (sz > db_cmd_copy_t::Max_buf_size - 100) {
        m_processor->add_buffer(std::move(m_current));
    }
}

