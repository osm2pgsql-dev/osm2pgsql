/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "params.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "overloaded.hpp"
#include "pgsql.hpp"

std::string to_string(param_value_t const &value)
{
    return std::visit(
        overloaded{[](null_param_t) { return std::string{}; },
                   [](std::string val) { return val; },
                   [](auto const &val) { return fmt::to_string(val); }},
        value);
}

param_value_t params_t::get(std::string const &key) const
{
    return m_map.at(key);
}

bool params_t::has(std::string const &key) const noexcept
{
    return m_map.count(key) > 0;
}

bool params_t::get_bool(std::string const &key, bool default_value) const
{
    return get_by_type<bool>(key, default_value);
}

int64_t params_t::get_int64(std::string const &key, int64_t default_value) const
{
    return get_by_type<int64_t>(key, default_value);
}

double params_t::get_double(std::string const &key, double default_value) const
{
    auto const it = m_map.find(key);
    if (it == m_map.end()) {
        return default_value;
    }

    if (std::holds_alternative<double>(it->second)) {
        return std::get<double>(it->second);
    }

    if (std::holds_alternative<int64_t>(it->second)) {
        return static_cast<double>(std::get<int64_t>(it->second));
    }

    throw fmt_error("Invalid value '{}' for {}.", to_string(it->second), key);
}

std::string params_t::get_string(std::string const &key) const
{
    auto const it = m_map.find(key);
    if (it == m_map.end()) {
        throw fmt_error("Missing parameter '{}' on generalizer.", key);
    }
    return to_string(it->second);
}

std::string params_t::get_string(std::string const &key,
                                 std::string const &default_value) const
{
    return get_by_type<std::string>(key, default_value);
}

std::string params_t::get_identifier(std::string const &key) const
{
    auto const it = m_map.find(key);
    if (it == m_map.end()) {
        return {};
    }
    std::string result = to_string(it->second);
    check_identifier(result, key.c_str());
    return result;
}

void params_t::check_identifier_with_default(std::string const &key,
                                             std::string default_value)
{
    auto const it = m_map.find(key);
    if (it == m_map.end()) {
        m_map.emplace(key, std::move(default_value));
    } else {
        check_identifier(to_string(it->second), key.c_str());
    }
}

unsigned int uint_in_range(params_t const &params, std::string const &key,
                           unsigned int min, unsigned int max,
                           unsigned int default_value)
{
    int64_t const value = params.get_int64(key, default_value);
    if (value < 0 || value > std::numeric_limits<unsigned int>::max()) {
        throw fmt_error("Invalid value '{}' for {}.", value, key);
    }
    auto uvalue = static_cast<unsigned int>(value);
    if (uvalue < min || uvalue > max) {
        throw fmt_error("Invalid value '{}' for {}.", value, key);
    }
    return uvalue;
}

void write_to_debug_log(params_t const &params, char const *message)
{
    if (!get_logger().debug_enabled()) {
        return;
    }
    log_debug(message);
    for (auto const &[key, value] : params) {
        log_debug("  {}={}", key, to_string(value));
    }
}
