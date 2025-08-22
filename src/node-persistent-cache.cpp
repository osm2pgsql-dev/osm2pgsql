/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "node-persistent-cache.hpp"

#include "logging.hpp"

#include <cassert>
#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>

void node_persistent_cache_t::set(osmid_t id, osmium::Location location)
{
    m_index->set(static_cast<osmium::unsigned_object_id_type>(id), location);
}

osmium::Location node_persistent_cache_t::get(osmid_t id) const noexcept
{
    return m_index->get_noexcept(
        static_cast<osmium::unsigned_object_id_type>(id));
}

node_persistent_cache_t::node_persistent_cache_t(std::string file_name,
                                                 bool create_file,
                                                 bool remove_file)
: m_file_name(std::move(file_name)), m_remove_file(remove_file)
{
    assert(!m_file_name.empty());

    log_debug("Loading persistent node cache from '{}'.", m_file_name);

    int flags = O_RDWR; // NOLINT(hicpp-signed-bitwise)
    if (create_file) {
        flags |= O_CREAT; // NOLINT(hicpp-signed-bitwise)
    }

#ifdef _WIN32
    flags |= O_BINARY; // NOLINT(hicpp-signed-bitwise)
#endif

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    m_fd = open(m_file_name.c_str(), flags, 0644);
    if (m_fd < 0) {
        throw std::system_error{
            errno, std::system_category(),
            fmt::format("Unable to open flatnode file '{}'", m_file_name)};
    }

    m_index = std::make_unique<index_t>(m_fd);

    // First location must always be the undefined location, otherwise we
    // might be looking at a different kind of file. This check here is for
    // forwards compatibility. If and when we change the file format, we
    // can use the first 8 bytes to differentiate the file format.
    auto const loc = get(0);
    if (loc.is_defined()) {
        throw fmt_error("Not a version 1 flatnode file '{}'", m_file_name);
    }
}

node_persistent_cache_t::~node_persistent_cache_t() noexcept
{
    m_index.reset();
    if (m_fd >= 0) {
        close(m_fd);
    }

    if (m_remove_file) {
        try {
            log_debug("Removing persistent node cache at '{}'.", m_file_name);
            std::error_code ec{};
            std::filesystem::remove(m_file_name, ec);
            if (ec) {
                log_warn("Failed to remove persistent node cache at '{}': {}.",
                         m_file_name, ec.message());
            }
        } catch (...) {
            // exception ignored on purpose
        }
    }
}
