#include <boost/format.hpp>
#include <cassert>
#include <cstdio>
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

void db_copy_thread_t::add_buffer(std::unique_ptr<db_copy_buffer_t> &&buffer)
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    m_worker_queue.push_back(std::move(buffer));
}

void db_copy_thread_t::finish()
{
    add_buffer(std::unique_ptr<db_copy_buffer_t>());
    m_worker.join();
}

void db_copy_thread_t::worker_thread()
{
    connect();

    for (;;) {
        std::unique_ptr<db_copy_buffer_t> item;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            if (m_worker_queue.empty()) {
                m_queue_cond.wait(lock);
                continue;
            }

            item = std::move(m_worker_queue.front());
            m_worker_queue.pop_front();
        }

        if (!item)
            break;

        if (item->is_copy_buffer())
            write_to_db(std::move(item));
        else
            execute_sql(item->buffer);
    }

    if (m_inflight)
        finish_copy();

    commit();
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
    // Wrap everything into one huge transaction. XXX is that a good idea?
    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, "BEGIN");
}

void db_copy_thread_t::execute_sql(std::string const &sql_cmd)
{
    if (m_inflight)
        finish_copy();

    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, sql_cmd.c_str());
}

void db_copy_thread_t::commit()
{
    if (!m_conn)
        return;

    fprintf(stderr, "Committing transactions\n");
    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, "COMMIT");

    PQfinish(m_conn);
    m_conn = nullptr;
}

void db_copy_thread_t::write_to_db(std::unique_ptr<db_copy_buffer_t> &&buffer)
{
    if (!buffer->deletables.empty() ||
        (m_inflight && !buffer->target->same_copy_target(*m_inflight->target)))
        finish_copy();

    if (!buffer->deletables.empty())
        delete_rows(buffer.get());

    start_copy(std::move(buffer));

    pgsql_CopyData(m_inflight->target->name.c_str(), m_conn,
                   m_inflight->buffer);
}

void db_copy_thread_t::delete_rows(db_copy_buffer_t *buffer)
{
    assert(!m_inflight);

    std::string sql = "DELETE FROM ";
    sql.reserve(buffer->target->name.size() + buffer->deletables.size() * 15 +
                30);
    sql += buffer->target->name;
    sql += "WHERE ";
    sql += buffer->target->id;
    sql += " IN (";
    for (auto id : buffer->deletables) {
        sql += std::to_string(id);
        sql += ',';
    }
    sql += ')';

    pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, sql);
}

void db_copy_thread_t::start_copy(std::unique_ptr<db_copy_buffer_t> &&buffer)
{
    if (!m_inflight) {
        std::string copystr = "COPY ";
        copystr.reserve(buffer->target->name.size() +
                        buffer->target->rows.size() + 14);
        copystr += buffer->target->name;
        if (!buffer->target->rows.empty()) {
            copystr += '(';
            copystr += buffer->target->rows;
            copystr += ')';
        }
        copystr += " FROM STDIN";
        pgsql_exec_simple(m_conn, PGRES_COPY_IN, copystr);
    }

    m_inflight = std::move(buffer);
}

void db_copy_thread_t::finish_copy()
{
    if (PQputCopyEnd(m_conn, nullptr) != 1)
        throw std::runtime_error((fmt("stop COPY_END for %1% failed: %2%\n") %
                                  m_inflight->target->name %
                                  PQerrorMessage(m_conn))
                                     .str());

    pg_result_t res(PQgetResult(m_conn));
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
        throw std::runtime_error((fmt("result COPY_END for %1% failed: %2%\n") %
                                  m_inflight->target->name %
                                  PQerrorMessage(m_conn))
                                     .str());

    m_inflight.reset();
}

db_copy_mgr_t::db_copy_mgr_t(std::shared_ptr<db_copy_thread_t> const &processor)
: m_processor(processor), m_last_line(0)
{}

void db_copy_mgr_t::new_line(std::shared_ptr<db_target_descr_t> const &table)
{
    if (!m_current || !m_current->target->same_copy_target(*table.get())) {
        if (m_current) {
            m_processor->add_buffer(std::move(m_current));
        }

        m_current.reset(new db_copy_buffer_t(table));
    }
}

void db_copy_mgr_t::delete_id(osmid_t osm_id)
{
    assert(m_current);
    m_current->deletables.push_back(osm_id);
}

void db_copy_mgr_t::exec_sql(std::string const &sql_cmd)
{
    // finish any ongoing copy operations
    if (m_current) {
        m_processor->add_buffer(std::move(m_current));
    }

    // and add SQL command
    m_current.reset(new db_copy_buffer_t(sql_cmd));
    m_processor->add_buffer(std::move(m_current));
}
