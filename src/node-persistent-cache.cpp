/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "logging.hpp"
#include "node-persistent-cache.hpp"

#include <cassert>
#include <stdexcept>
#include <system_error>

void node_persistent_cache::set(osmid_t id, osmium::Location location)
{
    m_index->set(static_cast<osmium::unsigned_object_id_type>(id), location);
}

osmium::Location node_persistent_cache::get(osmid_t id) const noexcept
{
    return m_index->get_noexcept(
        static_cast<osmium::unsigned_object_id_type>(id));
}

node_persistent_cache::node_persistent_cache(std::string file_name,
                                             bool remove_file)
: m_file_name(std::move(file_name)), m_remove_file(remove_file)
{
    assert(!m_file_name.empty());

    log_debug("Loading persistent node cache from '{}'.", m_file_name);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-signed-bitwise)
    m_fd = open(m_file_name.c_str(), O_RDWR | O_CREAT, 0644);
    if (m_fd < 0) {
        throw std::system_error{
            errno, std::system_category(),
            "Unable to open flatnode file '{}'"_format(m_file_name)};
    }

    m_index = std::make_unique<index_t>(m_fd);
}

node_persistent_cache::~node_persistent_cache() noexcept
{
    m_index.reset();
    if (m_fd >= 0) {
        close(m_fd);
    }

    if (m_remove_file) {
        try {
            log_debug("Removing persistent node cache at '{}'.", m_file_name);
        } catch (...) {
        }
        unlink(m_file_name.c_str());
    }
}
