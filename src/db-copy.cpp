/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-copy.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"

#include <cassert>
#include <cstdlib>
#include <iterator>
#include <stdexcept>

void db_deleter_by_id_t::delete_rows(std::string const &table,
                                     std::string const &column,
                                     pg_conn_t const &db_connection)
{
    fmt::memory_buffer sql;
    // Each deletable contributes an OSM ID and a comma. The highest node ID
    // currently has 10 digits, so 15 characters should do for a couple of years.
    // Add 50 characters for the SQL statement itself.
    sql.reserve(m_deletables.size() * 15 + 50);

    fmt::format_to(std::back_inserter(sql),
                   FMT_STRING("DELETE FROM {} WHERE {} IN ("), table, column);

    for (auto const id : m_deletables) {
        format_to(std::back_inserter(sql), FMT_STRING("{},"), id);
    }
    sql[sql.size() - 1] = ')';

    sql.push_back('\0');
    db_connection.exec(sql.data());
}

void db_deleter_by_type_and_id_t::delete_rows(std::string const &table,
                                              std::string const &column,
                                              pg_conn_t const &db_connection)
{
    assert(!m_deletables.empty());

    fmt::memory_buffer sql;
    // Need a VALUES line for each deletable: type (3 bytes), id (15 bytes),
    // braces etc. (4 bytes). And additional space for the remainder of the
    // SQL command.
    sql.reserve(m_deletables.size() * 22 + 200);

    if (m_has_type) {
        fmt::format_to(std::back_inserter(sql),
                       "DELETE FROM {} p USING (VALUES ", table);

        for (auto const &item : m_deletables) {
            fmt::format_to(std::back_inserter(sql), FMT_STRING("('{}',{}),"),
                           item.osm_type, item.osm_id);
        }

        sql.resize(sql.size() - 1);

        auto const pos = column.find(',');
        assert(pos != std::string::npos);
        std::string const type = column.substr(0, pos);

        fmt::format_to(std::back_inserter(sql),
                       ") AS t (osm_type, osm_id) WHERE"
                       " p.{} = t.osm_type::char(1) AND p.{} = t.osm_id",
                       type, column.c_str() + pos + 1);
    } else {
        fmt::format_to(std::back_inserter(sql),
                       FMT_STRING("DELETE FROM {} WHERE {} IN ("), table,
                       column);

        for (auto const &item : m_deletables) {
            format_to(std::back_inserter(sql), FMT_STRING("{},"), item.osm_id);
        }
        sql[sql.size() - 1] = ')';
    }

    sql.push_back('\0');
    db_connection.exec(sql.data());
}

db_copy_thread_t::db_copy_thread_t(connection_params_t const &connection_params)
{
    m_worker =
        std::thread{thread_t{pg_conn_t{connection_params, "copy"}, &m_shared}};
}

db_copy_thread_t::~db_copy_thread_t() { finish(); }

void db_copy_thread_t::send_command(db_cmd_t &&buffer)
{
    assert(m_worker.joinable()); // thread must not have been finished

    std::unique_lock<std::mutex> lock{m_shared.queue_mutex};
    m_shared.queue_full_cond.wait(lock, [&] {
        return m_shared.worker_queue.size() < db_cmd_copy_t::MAX_BUFFERS;
    });

    m_shared.worker_queue.push_back(std::move(buffer));
    m_shared.queue_cond.notify_one();
}

void db_copy_thread_t::end_copy()
{
    send_command(db_cmd_end_copy_t{});
}

void db_copy_thread_t::sync_and_wait()
{
    std::promise<void> barrier;
    std::future<void> const sync = barrier.get_future();
    send_command(db_cmd_sync_t{std::move(barrier)});
    sync.wait();
}

void db_copy_thread_t::finish()
{
    if (m_worker.joinable()) {
        send_command(db_cmd_finish_t{});
        m_worker.join();
    }
}

db_copy_thread_t::thread_t::thread_t(pg_conn_t &&db_connection,
                                     shared *shared)
: m_db_connection(std::move(db_connection)), m_shared(shared)
{}

void db_copy_thread_t::thread_t::operator()()
{
    try {
        // Disable sequential scan on database tables in the copy threads.
        // The copy threads only do COPYs (which are unaffected by this
        // setting) and DELETEs which we know benefit from the index. For
        // some reason PostgreSQL chooses in some cases not to use that index,
        // possibly because the DELETEs get a large list of ids to delete of
        // which many are not in the table which confuses the query planner.
        m_db_connection.exec("SET enable_seqscan = off");

        bool done = false;
        while (!done) {
            db_cmd_t item{};
            {
                std::unique_lock<std::mutex> lock{m_shared->queue_mutex};
                m_shared->queue_cond.wait(
                    lock, [&] { return !m_shared->worker_queue.empty(); });

                item = std::move(m_shared->worker_queue.front());
                m_shared->worker_queue.pop_front();
                m_shared->queue_full_cond.notify_one();
            }

            done = std::visit(
                [&](auto &&cmd) {
                    return execute(std::forward<decltype(cmd)>(cmd));
                },
                item);
        }

        finish_copy();
    } catch (std::runtime_error const &e) {
        log_error("DB copy thread failed: {}", e.what());
        std::exit(2); // NOLINT(concurrency-mt-unsafe)
    }
}

template <typename DELETER>
bool db_copy_thread_t::thread_t::execute(db_cmd_copy_delete_t<DELETER> &cmd)
{
    if (cmd.has_deletables() ||
        (m_inflight && !cmd.target->same_copy_target(*m_inflight))) {
        finish_copy();
    }

    cmd.delete_data(m_db_connection);

    if (!m_inflight) {
        start_copy(cmd.target);
    }

    m_db_connection.copy_send(cmd.buffer, cmd.target->name());

    return false;
}

bool db_copy_thread_t::thread_t::execute(db_cmd_end_copy_t &)
{
    finish_copy();
    return false;
}

bool db_copy_thread_t::thread_t::execute(db_cmd_sync_t &cmd)
{
    finish_copy();
    cmd.barrier.set_value();
    return false;
}

void db_copy_thread_t::thread_t::start_copy(
    std::shared_ptr<db_target_descr_t> const &target)
{
    assert(!m_inflight);

    auto const qname = qualified_name(target->schema(), target->name());
    fmt::memory_buffer sql;
    sql.reserve(qname.size() + target->rows().size() + 20);
    if (target->rows().empty()) {
        fmt::format_to(std::back_inserter(sql),
                       FMT_STRING("COPY {} FROM STDIN"), qname);
    } else {
        fmt::format_to(std::back_inserter(sql),
                       FMT_STRING("COPY {} ({}) FROM STDIN"), qname,
                       target->rows());
    }

    sql.push_back('\0');
    m_db_connection.copy_start(to_string(sql));

    m_inflight = target;
}

void db_copy_thread_t::thread_t::finish_copy()
{
    if (m_inflight) {
        m_db_connection.copy_end(m_inflight->name());
        m_inflight.reset();
    }
}
