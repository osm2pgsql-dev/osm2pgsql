#ifndef OSM2PGSQL_JSON_WRITER_HPP
#define OSM2PGSQL_JSON_WRITER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"

#include <cassert>
#include <cmath>
#include <iterator>
#include <string>
#include <type_traits>

class json_writer_t
{
public:
    void null() { m_buffer.append("null"); }

    void boolean(bool value) { m_buffer.append(value ? "true" : "false"); }

    template <typename T,
              std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
    void number(T value)
    {
        if (std::isfinite(value)) {
            m_buffer += fmt::to_string(value);
        } else {
            null();
        }
    }

    template <typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
    void number(T value)
    {
        m_buffer += fmt::to_string(value);
    }

    void string(char const *str)
    {
        m_buffer += '"';
        while (auto const c = *str++) {
            switch (c) {
            case '\b':
                m_buffer.append("\\b");
                break;
            case '\f':
                m_buffer.append("\\f");
                break;
            case '\n':
                m_buffer.append("\\n");
                break;
            case '\r':
                m_buffer.append("\\r");
                break;
            case '\t':
                m_buffer.append("\\t");
                break;
            case '"':
                m_buffer.append("\\\"");
                break;
            case '\\':
                m_buffer.append("\\\\");
                break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1fU) {
                    m_buffer.append(fmt::format(R"(\u{:04x})",
                                                static_cast<unsigned char>(c)));
                } else {
                    m_buffer += c;
                }
            }
        }
        m_buffer += '"';
    }

    void key(char const *key)
    {
        string(key);
        m_buffer += ':';
    }

    void start_object() { m_buffer += '{'; }

    void end_object()
    {
        assert(!m_buffer.empty());
        if (m_buffer.back() == ',') {
            m_buffer.back() = '}';
        } else {
            m_buffer += '}';
        }
    }

    void start_array() { m_buffer += '['; }

    void end_array()
    {
        assert(!m_buffer.empty());
        if (m_buffer.back() == ',') {
            m_buffer.back() = ']';
        } else {
            m_buffer += ']';
        }
    }

    void next() { m_buffer += ','; }

    std::string const &json() const noexcept { return m_buffer; }

private:
    std::string m_buffer;
};

#endif // OSM2PGSQL_JSON_WRITER_HPP
