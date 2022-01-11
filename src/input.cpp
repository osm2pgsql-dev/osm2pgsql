/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include "format.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "osmdata.hpp"
#include "progress-display.hpp"

type_id check_input(type_id const &last, type_id curr)
{
    if (curr.id < 0) {
        throw std::runtime_error{
            "Negative OSM object ids are not allowed: {} id {}."_format(
                osmium::item_type_to_name(curr.type), curr.id)};
    }

    if (last.type == curr.type) {
        if (last.id < curr.id) {
            return curr;
        }

        if (last.id > curr.id) {
            throw std::runtime_error{
                "Input data is not ordered: {} id {} after {}."_format(
                    osmium::item_type_to_name(last.type), curr.id, last.id)};
        }

        throw std::runtime_error{
            "Input data is not ordered:"
            " {} id {} appears more than once."_format(
                osmium::item_type_to_name(last.type), curr.id)};
    }

    if (item_type_to_nwr_index(last.type) <=
        item_type_to_nwr_index(curr.type)) {
        return curr;
    }

    throw std::runtime_error{"Input data is not ordered: {} after {}."_format(
        osmium::item_type_to_name(curr.type),
        osmium::item_type_to_name(last.type))};
}

type_id check_input(type_id const &last, osmium::OSMObject const &object)
{
    return check_input(last, {object.type(), object.id()});
}

/**
 * A data source is where we get the OSM objects from, one at a time. It
 * wraps the osmium::io::Reader.
 */
class data_source_t
{
public:
    explicit data_source_t(osmium::io::File const &file)
    : m_reader(std::make_unique<osmium::io::Reader>(file))
    {
        get_next_nonempty_buffer();
        m_last = check_input(m_last, *m_it);
    }

    bool empty() const noexcept { return !m_buffer; }

    bool next()
    {
        assert(!empty());
        ++m_it;

        while (m_it == m_end) {
            if (!get_next_nonempty_buffer()) {
                return false;
            }
        }

        m_last = check_input(m_last, *m_it);
        return true;
    }

    osmium::OSMObject *get() noexcept
    {
        assert(!empty());
        return &*m_it;
    }

    std::size_t offset() const noexcept { return m_reader->offset(); }

    void close()
    {
        m_reader->close();
        m_reader.reset();
    }

private:
    bool get_next_nonempty_buffer()
    {
        while ((m_buffer = m_reader->read())) {
            m_it = m_buffer.begin<osmium::OSMObject>();
            m_end = m_buffer.end<osmium::OSMObject>();
            if (m_it != m_end) {
                return true;
            }
        }
        return false;
    }

    using iterator = osmium::memory::Buffer::t_iterator<osmium::OSMObject>;

    std::unique_ptr<osmium::io::Reader> m_reader;
    osmium::memory::Buffer m_buffer{};
    iterator m_it{};
    iterator m_end{};
    type_id m_last = {osmium::item_type::node, 0};

}; // class data_source_t

/**
 * A element in a priority queue of OSM objects. Holds a pointer to the OSM
 * object as well as a pointer to the source the OSM object came from.
 */
class queue_element_t
{
public:
    queue_element_t(osmium::OSMObject *object, data_source_t *source) noexcept
    : m_object(object), m_source(source)
    {}

    osmium::OSMObject const &object() const noexcept { return *m_object; }

    osmium::OSMObject &object() noexcept { return *m_object; }

    data_source_t *data_source() const noexcept { return m_source; }

    friend bool operator<(queue_element_t const &lhs,
                          queue_element_t const &rhs) noexcept
    {
        // This is needed for the priority queue. We want objects with smaller
        // id (and earlier versions of the same object) to come first, but
        // the priority queue expects largest first. So we need to reverse the
        // comparison here.
        return lhs.object() > rhs.object();
    }

    friend bool operator==(queue_element_t const &lhs,
                           queue_element_t const &rhs) noexcept
    {
        return lhs.object().type() == rhs.object().type() &&
               lhs.object().id() == rhs.object().id();
    }

    friend bool operator!=(queue_element_t const &lhs,
                           queue_element_t const &rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    osmium::OSMObject *m_object;
    data_source_t *m_source;

}; // class queue_element_t

std::vector<osmium::io::File>
prepare_input_files(std::vector<std::string> const &input_files,
                    std::string const &input_format, bool append)
{
    std::vector<osmium::io::File> files;

    for (auto const &filename : input_files) {
        osmium::io::File file{filename, input_format};

        if (file.format() == osmium::io::file_format::unknown) {
            if (input_format.empty()) {
                throw std::runtime_error{
                    "Cannot detect file format for '{}'. Try using -r."_format(
                        filename)};
            }
            throw std::runtime_error{
                "Unknown file format '{}'."_format(input_format)};
        }

        if (!append && file.has_multiple_object_versions()) {
            throw std::runtime_error{
                "Reading an OSM change file only works in append mode."};
        }

        log_debug("Reading file: {}", filename);

        files.emplace_back(file);
    }

    return files;
}

class input_context_t
{
public:
    input_context_t(osmdata_t *osmdata, progress_display_t *progress,
                    bool append)
    : m_osmdata(osmdata), m_progress(progress), m_append(append)
    {
        assert(osmdata);
        assert(progress);
    }

    void apply(osmium::OSMObject &object)
    {
        if (!m_append && object.deleted()) {
            throw std::runtime_error{"Input file contains deleted objects but "
                                     "you are not in append mode."};
        }

        if (m_last_type != object.type()) {
            if (m_last_type == osmium::item_type::node) {
                m_osmdata->after_nodes();
                m_progress->start_way_counter();
            }
            if (object.type() == osmium::item_type::relation) {
                m_osmdata->after_ways();
                m_progress->start_relation_counter();
            }
            m_last_type = object.type();
        }

        osmium::apply_item(object, *m_osmdata, *m_progress);
    }

    void eof()
    {
        switch (m_last_type) {
        case osmium::item_type::node:
            m_osmdata->after_nodes();
            // fallthrough
        case osmium::item_type::way:
            m_osmdata->after_ways();
            break;
        default:
            break;
        }

        m_osmdata->after_relations();
        m_progress->print_summary();
    }

private:
    osmdata_t *m_osmdata;
    progress_display_t *m_progress;
    osmium::item_type m_last_type = osmium::item_type::node;
    bool m_append;
}; // class input_context_t

static void process_single_file(osmium::io::File const &file,
                                osmdata_t *osmdata,
                                progress_display_t *progress, bool append)
{
    osmium::io::Reader reader{file};
    type_id last{osmium::item_type::node, 0};

    input_context_t ctx{osmdata, progress, append};
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (auto &object : buffer.select<osmium::OSMObject>()) {
            last = check_input(last, object);
            ctx.apply(object);
        }
    }
    ctx.eof();

    reader.close();
}

static void process_multiple_files(std::vector<osmium::io::File> const &files,
                                   osmdata_t *osmdata,
                                   progress_display_t *progress, bool append)
{
    std::vector<data_source_t> data_sources;
    data_sources.reserve(files.size());

    std::priority_queue<queue_element_t> queue;

    for (osmium::io::File const &file : files) {
        data_sources.emplace_back(file);

        if (!data_sources.back().empty()) {
            queue.emplace(data_sources.back().get(), &data_sources.back());
        }
    }

    input_context_t ctx{osmdata, progress, append};
    while (!queue.empty()) {
        auto element = queue.top();
        queue.pop();
        if (queue.empty() || element != queue.top()) {
            ctx.apply(element.object());
        }

        auto *source = element.data_source();
        if (source->next()) {
            queue.emplace(source->get(), source);
        }
    }
    ctx.eof();

    for (auto &data_source : data_sources) {
        data_source.close();
    }
}

void process_files(std::vector<osmium::io::File> const &files,
                   osmdata_t *osmdata, bool append, bool show_progress)
{
    assert(osmdata);

    progress_display_t progress{show_progress};

    if (files.size() == 1) {
        process_single_file(files.front(), osmdata, &progress, append);
    } else {
        process_multiple_files(files, osmdata, &progress, append);
    }
}
