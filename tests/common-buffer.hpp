#ifndef OSM2PGSQL_TESTS_COMMON_BUFFER_HPP
#define OSM2PGSQL_TESTS_COMMON_BUFFER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "osmtypes.hpp"

#include <osmium/memory/buffer.hpp>
#include <osmium/opl.hpp>
#include <osmium/osm.hpp>

/**
 * Wrapper around an Osmium buffer to create test objects in with some
 * convenience.
 */
class test_buffer_t
{
public:
    osmium::memory::Buffer const &buffer() const noexcept { return m_buffer; }

    osmium::Node const &add_node(std::string const &data)
    {
        return m_buffer.get<osmium::Node>(add_opl(data));
    }

    osmium::Way &add_way(std::string const &data)
    {
        return m_buffer.get<osmium::Way>(add_opl(data));
    }

    osmium::Way &add_way(osmid_t wid, idlist_t const &ids)
    {
        assert(!ids.empty());
        std::string nodes;

        for (auto const id : ids) {
            nodes += "n{},"_format(id);
        }

        nodes.resize(nodes.size() - 1);

        return add_way("w{} N{}"_format(wid, nodes));
    }

    osmium::Relation const &add_relation(std::string const &data)
    {
        return m_buffer.get<osmium::Relation>(add_opl(data));
    }

private:
    std::size_t add_opl(std::string const &data)
    {
        auto const offset = m_buffer.committed();
        osmium::opl_parse(data.c_str(), m_buffer);
        return offset;
    }

    osmium::memory::Buffer m_buffer{4096,
                                    osmium::memory::Buffer::auto_grow::yes};
};

#endif // OSM2PGSQL_TESTS_COMMON_BUFFER_HPP
