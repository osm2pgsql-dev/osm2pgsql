#ifndef OSM2PGSQL_PARAMS_HPP
#define OSM2PGSQL_PARAMS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <variant>

/// A "NULL" value for a parameter. Same as not set.
using null_param_t = std::monostate;

/// A parameter value can have one of several types.
using param_value_t =
    std::variant<null_param_t, std::string, int64_t, double, bool>;

/// Convert a parameter value into a string.
std::string to_string(param_value_t const &value);

/**
 * A collection of parameters.
 */
class params_t
{
public:
    template <typename K, typename V>
    void set(K &&key, V &&value)
    {
        m_map.insert_or_assign(std::forward<K>(key), std::forward<V>(value));
    }

    template <typename K>
    void remove(K &&key)
    {
        m_map.erase(std::forward<K>(key));
    }

    bool has(std::string const &key) const noexcept;

    param_value_t get(std::string const &key) const;

    bool get_bool(std::string const &key, bool default_value = false) const;

    int64_t get_int64(std::string const &key, int64_t default_value = 0) const;

    double get_double(std::string const &key, double default_value = 0.0) const;

    std::string get_string(std::string const &key) const;

    std::string get_string(std::string const &key,
                           std::string const &default_value) const;

    std::string get_identifier(std::string const &key) const;

    void check_identifier_with_default(std::string const &key,
                                       std::string default_value);

    auto begin() const noexcept { return m_map.begin(); }

    auto end() const noexcept { return m_map.end(); }

private:
    template <typename T>
    T get_by_type(std::string const &key, T default_value) const
    {
        auto const it = m_map.find(key);
        if (it == m_map.end()) {
            return default_value;
        }

        if (!std::holds_alternative<T>(it->second)) {
            throw fmt_error("Invalid value '{}' for {}.", to_string(it->second),
                            key);
        }
        return std::get<T>(it->second);
    }

    std::map<std::string, param_value_t> m_map;
}; // class params_t

void write_to_debug_log(params_t const &params, char const *message);

unsigned int uint_in_range(params_t const &params, std::string const &key,
                           unsigned int min, unsigned int max,
                           unsigned int default_value);

#endif // OSM2PGSQL_PARAMS_HPP
