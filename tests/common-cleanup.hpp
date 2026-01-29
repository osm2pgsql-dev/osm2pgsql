#ifndef OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
#define OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"

#include <filesystem>
#include <string>

namespace testing::cleanup {

/**
 * RAII structure to remove a file upon destruction.
 *
 * Per default will also make sure that the file does not exist
 * when it is constructed.
 */
class file_t
{
public:
    explicit file_t(std::string filename, bool remove_on_construct = true)
    : m_filename(std::move(filename))
    {
        if (remove_on_construct) {
            delete_file(false);
        }
    }

    file_t(file_t const &) = delete;
    file_t &operator=(file_t const &) = delete;

    file_t(file_t &&) = delete;
    file_t &operator=(file_t const &&) = delete;

    ~file_t() noexcept { delete_file(true); }

private:
    // This function is run from a destructor so must be noexcept. If an
    // exception does occur the program is terminated and we are fine with
    // that, it is test code after all.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    void delete_file(bool warn) const noexcept
    {
        if (m_filename.empty()) {
            return;
        }

        std::error_code ec;
        if (!std::filesystem::remove(m_filename, ec) && warn) {
            fmt::print(stderr, "WARNING: Unable to remove \"{}\": {}\n",
                       m_filename, ec.message());
        }
    }

    std::string m_filename;
};

} // namespace testing::cleanup

#endif // OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
