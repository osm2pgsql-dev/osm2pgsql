#ifndef OSM2PGSQL_DB_COPY_MGR_HPP
#define OSM2PGSQL_DB_COPY_MGR_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <osmium/osm/timestamp.hpp>

#include "db-copy.hpp"
#include "format.hpp"
#include "hex.hpp"

/**
 * Convert a double to a float for a real column. Values that PostgreSQL
 * would reject as out of range for type real in the text format (overflow
 * to infinity or underflow to zero) throw an exception.
 */
inline float copy_to_float4(double value)
{
    if (std::isnan(value)) { // the text format sends every NaN as "nan"
        return std::numeric_limits<float>::quiet_NaN();
    }

    auto const result = static_cast<float>(value);
    if (std::isfinite(value) &&
        (std::isinf(result) || (result == 0.0F && value != 0.0))) {
        throw fmt_error("Value {} is out of range for type real.", value);
    }

    return result;
}

/**
 * Management class that fills and manages copy buffers.
 *
 * Rows are written in PostgreSQL's text COPY format, or in its binary COPY
 * format if the target has binary_types() set. The same calls are used for
 * both, in the binary format the type of each field is taken from the
 * target's list of column types.
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
        m_binary_types = table->binary() ? &table->binary_types() : nullptr;
        m_field = 0;
    }

    void rollback_line()
    {
        assert(m_current);
        m_current.buffer.resize(m_committed);
        m_field = 0;
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

        if (m_binary_types) {
            // Binary rows have no delimiter, but all fields must be there.
            assert(m_field == m_binary_types->size());
        } else {
            // Expect that a column has been written last which ended in a
            // '\t'. Replace it with the row delimiter '\n'.
            assert(buf.back() == '\t');
            buf.back() = '\n';
        }

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
        if (m_binary_types) {
            add_binary(next_field(), std::forward<T>(value));
            return;
        }
        add_value(std::forward<T>(value));
        m_current.buffer += '\t';
    }

    /**
     * Add an empty column.
     *
     * Adds a NULL value for the column.
     */
    void add_null_column()
    {
        if (m_binary_types) {
            next_field();
            put_int32(-1); // NULL
            return;
        }
        m_current.buffer += "\\N\t";
    }

    /**
     * Start an array column.
     *
     * An array is a list of simple elements of the same type.
     *
     * Must be finished with a call to finish_array().
     */
    void new_array()
    {
        if (m_binary_types) {
            check_field_type(next_field(), copy_field_type::int8_array);
            m_field_start = start_field_length();
            put_int32(1);       // number of dimensions
            put_int32(0);       // no NULL elements
            put_int32(INT8OID); // element type
            put_int32(0);       // length, set in finish_array()
            put_int32(1);       // lower bound
            return;
        }
        m_current.buffer += "{";
    }

    /**
     * Add a single value to an array column.
     *
     * Adds the value in the format appropriate for an array and a value
     * separator.
     */
    void add_array_elem(osmid_t value)
    {
        if (m_binary_types) {
            put_value(value);
            return;
        }
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
        if (m_binary_types) {
            auto const header = m_field_start + 4;
            auto const elements =
                (m_current.buffer.size() - header - ARRAY_HEADER_SIZE) /
                ARRAY_ELEMENT_SIZE;
            if (elements == 0) {
                // An empty array has zero dimensions and no dimension info.
                m_current.buffer.resize(header + 12);
                put_length_at(header, 0); // number of dimensions
            } else {
                put_length_at(header + 12, elements);
            }
            finish_field_length(m_field_start);
            return;
        }
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
    {
        if (m_binary_types) {
            check_field_type(next_field(), copy_field_type::hstore);
            m_field_start = start_field_length();
            put_int32(0); // pairs, set in finish_hash()
            m_hash_pairs = 0;
        }
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
        if (m_binary_types) {
            add_binary_hash_elem(k, v);
            return;
        }
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
        if (m_binary_types) {
            add_binary_hash_elem(k, v);
            return;
        }
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
        if (m_binary_types) {
            add_binary_hash_elem(k, std::to_string(value).c_str());
            return;
        }
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
        if (m_binary_types) {
            put_length_at(m_field_start + 4, m_hash_pairs);
            finish_field_length(m_field_start);
            return;
        }
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
     * The geometry is converted on-the-fly from WKB binary to WKB hex. In
     * the binary format the WKB is sent as is.
     */
    void add_hex_geom(std::string const &wkb)
    {
        if (m_binary_types) {
            check_field_type(next_field(), copy_field_type::geometry);
            add_binary_bytes(wkb);
            return;
        }
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
    void delete_object(ARGS &&...args)
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
    /// OID of the int8 type, needed for the elements of int8[].
    static constexpr int32_t INT8OID = 20;
    /// Array header: ndim, flags, element type, one dimension, lower bound
    static constexpr std::size_t ARRAY_HEADER_SIZE = 20;
    /// Array element: length and int8 value
    static constexpr std::size_t ARRAY_ELEMENT_SIZE = 12;

    /**
     * Get the type of the next field of the current row in binary format.
     * A row starts with the number of fields, that is written together with
     * the first field, so that nothing is left in the buffer from a
     * new_line() that is only used to delete objects.
     */
    copy_field_type next_field()
    {
        assert(m_binary_types);
        assert(m_field < m_binary_types->size());
        if (m_field == 0) {
            put_int16(static_cast<int16_t>(m_binary_types->size()));
        }
        return (*m_binary_types)[m_field++];
    }

    static void check_field_type(copy_field_type type, copy_field_type expected)
    {
        if (type != expected) {
            throw_type_mismatch(type);
        }
    }

    [[noreturn]] static void throw_type_mismatch(copy_field_type type)
    {
        throw fmt_error("Internal error: Wrong data for column of type {} in"
                        " binary COPY.",
                        static_cast<int>(type));
    }

    /// Is the integer value representable in the (signed) type R?
    template <typename R, typename T>
    static constexpr bool fits_in(T value) noexcept
    {
        if constexpr (std::is_signed_v<T>) {
            return value >= std::numeric_limits<R>::min() &&
                   value <= std::numeric_limits<R>::max();
        } else {
            return static_cast<std::uintmax_t>(value) <=
                   static_cast<std::uintmax_t>(std::numeric_limits<R>::max());
        }
    }

    /**
     * The bytes of an integer in big-endian (network) byte order, which is
     * what the binary format uses. Compilers turn this into a single byte
     * swap instruction.
     */
    template <typename T, std::size_t... I>
    static constexpr std::array<char, sizeof(T)>
    big_endian_bytes(T value, std::index_sequence<I...> /*bytes*/) noexcept
    {
        auto const v =
            static_cast<uint64_t>(static_cast<std::make_unsigned_t<T>>(value));
        return {static_cast<char>(v >> (8U * (sizeof(T) - 1 - I)))...};
    }

    template <typename T>
    static constexpr std::array<char, sizeof(T)>
    big_endian_bytes(T value) noexcept
    {
        return big_endian_bytes(value, std::make_index_sequence<sizeof(T)>{});
    }

    template <typename T>
    void put_be(T value)
    {
        auto const bytes = big_endian_bytes(value);
        m_current.buffer.append(bytes.data(), bytes.size());
    }

    void put_int16(int16_t value) { put_be(value); }
    void put_int32(int32_t value) { put_be(value); }

    /// Write the length of a field or the number of elements of something.
    void put_length(std::size_t length)
    {
        assert(length <= std::numeric_limits<int32_t>::max());
        put_int32(static_cast<int32_t>(length));
    }

    /// Overwrite an int32 written earlier at position pos.
    void put_length_at(std::size_t pos, std::size_t length)
    {
        assert(length <= std::numeric_limits<int32_t>::max());
        auto const bytes = big_endian_bytes(static_cast<int32_t>(length));
        m_current.buffer.replace(pos, bytes.size(), bytes.data(), bytes.size());
    }

    /// Write a complete field with a fixed-size value: length, then value.
    template <typename T>
    void put_value(T value)
    {
        put_length(sizeof(T));
        put_be(value);
    }

    /// Reserve space for the length of a field, return its position.
    std::size_t start_field_length()
    {
        auto const pos = m_current.buffer.size();
        m_current.buffer.append(4, '\0');
        return pos;
    }

    void finish_field_length(std::size_t pos)
    {
        put_length_at(pos, m_current.buffer.size() - pos - 4);
    }

    void add_binary_bytes(std::string_view data)
    {
        put_length(data.size());
        m_current.buffer += data;
    }

    void add_binary(copy_field_type type, std::string_view str)
    {
        switch (type) {
        case copy_field_type::text:
            add_binary_bytes(str);
            break;
        case copy_field_type::jsonb:
            put_length(str.size() + 1);
            m_current.buffer += '\1'; // jsonb format version
            m_current.buffer += str;
            break;
        default:
            throw_type_mismatch(type);
        }
    }

    void add_binary(copy_field_type type, char const *str)
    {
        add_binary(type, std::string_view{str});
    }

    void add_binary(copy_field_type type, std::string const &str)
    {
        add_binary(type, std::string_view{str});
    }

    void add_binary(copy_field_type type, osmium::Timestamp timestamp)
    {
        check_field_type(type, copy_field_type::timestamptz);
        if (!timestamp.valid()) {
            // The text format sends an empty string, which is invalid, too.
            throw std::runtime_error{"Invalid timestamp (0)."};
        }
        // Microseconds since 2000-01-01 00:00:00 UTC
        constexpr int64_t PG_EPOCH = 946684800;
        put_value(
            (static_cast<int64_t>(timestamp.seconds_since_epoch()) - PG_EPOCH) *
            1000000);
    }

    template <typename T>
    std::enable_if_t<std::is_arithmetic_v<T>> add_binary(copy_field_type type,
                                                         T value)
    {
        if constexpr (std::is_same_v<T, char>) {
            check_field_type(type, copy_field_type::text);
            add_binary_bytes(std::string_view{&value, 1});
            return;
        }

        switch (type) {
        case copy_field_type::boolean:
            put_length(1);
            m_current.buffer += (value != 0) ? '\1' : '\0';
            return;
        case copy_field_type::float4: {
            float const f = copy_to_float4(static_cast<double>(value));
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            put_value(bits);
            return;
        }
        case copy_field_type::float8: {
            auto d = static_cast<double>(value);
            if (std::isnan(d)) {
                d = std::numeric_limits<double>::quiet_NaN();
            }
            uint64_t bits = 0;
            std::memcpy(&bits, &d, sizeof(bits));
            put_value(bits);
            return;
        }
        case copy_field_type::text:
            add_binary_bytes(fmt::to_string(value));
            return;
        default:
            break;
        }

        if constexpr (std::is_integral_v<T>) {
            switch (type) {
            case copy_field_type::int2:
                assert(fits_in<int16_t>(value));
                put_value(static_cast<int16_t>(value));
                return;
            case copy_field_type::int4:
                assert(fits_in<int32_t>(value));
                put_value(static_cast<int32_t>(value));
                return;
            case copy_field_type::int8:
                put_value(static_cast<int64_t>(value));
                return;
            default:
                break;
            }
        }

        throw_type_mismatch(type);
    }

    void add_binary_hash_elem(char const *k, char const *v)
    {
        add_binary_bytes(k);
        add_binary_bytes(v);
        ++m_hash_pairs;
    }

    template <typename T>
    void add_value(T value)
    {
        m_current.buffer += fmt::to_string(value);
    }

    void add_value(osmium::Timestamp timestamp)
    {
        m_current.buffer += timestamp.to_iso();
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

    /// Column types of the current target if it uses the binary format.
    std::vector<copy_field_type> const *m_binary_types = nullptr;
    /// Number of the next field in the current row (binary format).
    std::size_t m_field = 0;
    /// Start of the array or hash field being written (binary format).
    std::size_t m_field_start = 0;
    /// Number of pairs in the hash field being written (binary format).
    std::size_t m_hash_pairs = 0;
};

#endif // OSM2PGSQL_DB_COPY_MGR_HPP
