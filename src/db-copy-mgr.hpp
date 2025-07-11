#ifndef OSM2PGSQL_DB_COPY_MGR_HPP
#define OSM2PGSQL_DB_COPY_MGR_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cassert>
#include <memory>
#include <string>

#include "db-copy.hpp"
#include "hex.hpp"

/**
 * Management class that fills and manages copy buffers.
 */
template <typename DELETER>
class db_copy_mgr_t
{
public:
    explicit db_copy_mgr_t(std::shared_ptr<db_copy_thread_t> processor)
    : m_processor(std::move(processor))
    {}

    /**
     * Start a new table row.
     *
     * Also starts a new buffer if either the table is not the same as
     * the table of currently buffered data or no buffer is pending.
     */
    void new_line(std::shared_ptr<db_target_descr_t> const &table)
    {
        if (!m_current || !m_current.target->same_copy_target(*table)) {
            if (m_current) {
                m_processor->send_command(std::move(m_current));
            }
            m_current = db_cmd_copy_delete_t<DELETER>(table);
        }
        m_committed = m_current.buffer.size();
    }

    void rollback_line()
    {
        assert(m_current);
        m_current.buffer.resize(m_committed);
    }

    /**
     * Finish a table row.
     *
     * Adds the row delimiter to the buffer. If the buffer is at capacity
     * it will be forwarded to the copy thread.
     */
    void finish_line()
    {
        assert(m_current);

        auto &buf = m_current.buffer;
        assert(!buf.empty());

        // Expect that a column has been written last which ended in a '\t'.
        // Replace it with the row delimiter '\n'.
        assert(buf.back() == '\t');
        buf.back() = '\n';

        if (m_current.is_full()) {
            m_processor->send_command(std::move(m_current));
            m_current = {};
        }
    }

    /**
     * Add many simple columns.
     *
     * See add_column().
     */
    template <typename... ARGS>
    void add_columns(ARGS &&...args)
    {
        (add_column(std::forward<ARGS>(args)), ...);
    }

    /**
     * Add a column entry of simple type.
     *
     * Writes the column with the escaping appropriate for the type and
     * a column delimiter.
     */
    template <typename T>
    void add_column(T &&value)
    {
        add_value(std::forward<T>(value));
        m_current.buffer += '\t';
    }

    /**
     * Add an empty column.
     *
     * Adds a NULL value for the column.
     */
    void add_null_column() { m_current.buffer += "\\N\t"; }

    /**
     * Start an array column.
     *
     * An array is a list of simple elements of the same type.
     *
     * Must be finished with a call to finish_array().
     */
    void new_array() { m_current.buffer += "{"; }

    /**
     * Add a single value to an array column.
     *
     * Adds the value in the format appropriate for an array and a value
     * separator.
     */
    void add_array_elem(osmid_t value)
    {
        add_value(value);
        m_current.buffer += ',';
    }

    /**
     * Finish an array column previously started with new_array().
     *
     * The array may be empty. If it does contain elements, the separator after
     * the final element is replaced with the closing array bracket.
     */
    void finish_array()
    {
        assert(!m_current.buffer.empty());
        if (m_current.buffer.back() == '{') {
            m_current.buffer += '}';
        } else {
            m_current.buffer.back() = '}';
        }
        m_current.buffer += '\t';
    }

    /**
     * Start a hash column.
     *
     * A hash column contains a list of key/value pairs. May be represented
     * by a hstore or json in Postgresql.
     *
     * currently a hstore column is written which does not have any start
     * markers.
     *
     * Must be closed with a finish_hash() call.
     */
    void new_hash()
    { /* nothing */
    }

    void add_hash_elem(std::string const &k, std::string const &v)
    {
        add_hash_elem(k.c_str(), v.c_str());
    }

    /**
     * Add a key/value pair to a hash column.
     *
     * Key and value must be strings and will be appropriately escaped.
     * A separator for the next pair is added at the end.
     */
    void add_hash_elem(char const *k, char const *v)
    {
        m_current.buffer += '"';
        add_escaped_string(k);
        m_current.buffer += "\"=>\"";
        add_escaped_string(v);
        m_current.buffer += "\",";
    }

    /**
     * Add a key/value pair to a hash column without escaping.
     *
     * Key and value must be strings and will NOT be appropriately escaped.
     * A separator for the next pair is added at the end.
     */
    void add_hash_elem_noescape(char const *k, char const *v)
    {
        m_current.buffer += '"';
        m_current.buffer += k;
        m_current.buffer += "\"=>\"";
        m_current.buffer += v;
        m_current.buffer += "\",";
    }

    /**
     * Add a key (unescaped) and a numeric value to a hash column.
     *
     * Key must be string and come from a safe source because it will NOT be
     * escaped! The value should be convertible using std::to_string.
     * A separator for the next pair is added at the end.
     *
     * This method is suitable to insert safe input, e.g. numeric OSM metadata
     * (eg. uid) but not unsafe input like user names.
     */
    template <typename T>
    void add_hstore_num_noescape(char const *k, T const value)
    {
        m_current.buffer += '"';
        m_current.buffer += k;
        m_current.buffer += "\"=>\"";
        m_current.buffer += std::to_string(value);
        m_current.buffer += "\",";
    }

    /**
     * Close a hash previously started with new_hash().
     *
     * The hash may be empty. If elements were present, the separator
     * of the final element is overwritten with the closing \t.
     */
    void finish_hash()
    {
        auto const idx = m_current.buffer.size() - 1;
        if (!m_current.buffer.empty() && m_current.buffer[idx] == ',') {
            m_current.buffer[idx] = '\t';
        } else {
            m_current.buffer += '\t';
        }
    }

    /**
     * Add a column with the given WKB geometry in WKB hex format.
     *
     * The geometry is converted on-the-fly from WKB binary to WKB hex.
     */
    void add_hex_geom(std::string const &wkb)
    {
        util::encode_hex(wkb, &m_current.buffer);
        m_current.buffer += '\t';
    }

    /**
     * Mark an OSM object for deletion in the current table.
     *
     * The object is guaranteed to be deleted before any lines
     * following the delete_object() are inserted.
     */
    template <typename... ARGS>
    void delete_object(ARGS &&... args)
    {
        assert(m_current);
        m_current.add_deletable(std::forward<ARGS>(args)...);
    }

    void flush()
    {
        // flush current buffer if there is one
        if (m_current) {
            m_processor->send_command(std::move(m_current));
            m_current = {};
        }
        // close any ongoing copy operations
        m_processor->end_copy();
    }

    /**
     * Synchronize with worker.
     *
     * Only returns when all previously issued commands are done.
     */
    void sync()
    {
        flush();
        m_processor->sync_and_wait();
    }

private:
    template <typename T>
    void add_value(T value)
    {
        m_current.buffer += fmt::to_string(value);
    }

    void add_value(std::string const &s) { add_value(s.c_str()); }

    void add_value(char const *s)
    {
        assert(m_current);
        for (char const *c = s; *c; ++c) {
            switch (*c) {
            case '"':
                m_current.buffer += "\\\"";
                break;
            case '\\':
                m_current.buffer += "\\\\";
                break;
            case '\n':
                m_current.buffer += "\\n";
                break;
            case '\r':
                m_current.buffer += "\\r";
                break;
            case '\t':
                m_current.buffer += "\\t";
                break;
            default:
                m_current.buffer += *c;
                break;
            }
        }
    }

    void add_escaped_string(char const *s)
    {
        for (char const *c = s; *c; ++c) {
            switch (*c) {
            case '"':
                m_current.buffer += R"(\\")";
                break;
            case '\\':
                m_current.buffer += R"(\\\\)";
                break;
            case '\n':
                m_current.buffer += "\\n";
                break;
            case '\r':
                m_current.buffer += "\\r";
                break;
            case '\t':
                m_current.buffer += "\\t";
                break;
            default:
                m_current.buffer += *c;
                break;
            }
        }
    }

    std::shared_ptr<db_copy_thread_t> m_processor;
    db_cmd_copy_delete_t<DELETER> m_current;
    std::size_t m_committed = 0;
};

#endif // OSM2PGSQL_DB_COPY_MGR_HPP
