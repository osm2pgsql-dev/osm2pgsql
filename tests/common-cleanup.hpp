#ifndef OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
#define OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"

#include <filesystem>
#include <string>

namespace testing {
namespace cleanup {

/**
 * RAII structure to remove a file upon destruction.
 *
 * Per default will also make sure that the file does not exist
 * when it is constructed.
 */
class file_t
{
public:
    file_t(std::string const &filename, bool remove_on_construct = true)
    : m_filename(filename)
    {
        if (remove_on_construct) {
            delete_file(false);
        }
    }

    ~file_t() noexcept { delete_file(true); }

private:
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

} // namespace cleanup
} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
