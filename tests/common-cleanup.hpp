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

#include <string>

// for unlink()
#ifdef _WIN32
#include <io.h>
#include <cstdio>
#else
#include <unistd.h>
#endif

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

        int result = unlink(m_filename.c_str());
        if (result != 0 && warn) {
            fmt::print(stderr, "WARNING: Unable to remove \"{}\": {}\n",
                       m_filename, std::strerror(result));
        }
    }

    std::string m_filename;
};

} // namespace cleanup
} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
