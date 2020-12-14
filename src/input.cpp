
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

type_id_version check_input(type_id_version const &last, type_id_version curr)
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

        if (last.version < curr.version) {
            return curr;
        }

        throw std::runtime_error{
            "Input data is not ordered: {} id {} version {} after {}."_format(
                osmium::item_type_to_name(last.type), curr.id, curr.version,
                last.version)};
    }

    if (item_type_to_nwr_index(last.type) <=
        item_type_to_nwr_index(curr.type)) {
        return curr;
    }

    throw std::runtime_error{"Input data is not ordered: {} after {}."_format(
        osmium::item_type_to_name(curr.type),
        osmium::item_type_to_name(last.type))};
}

type_id_version check_input(type_id_version const &last,
                            osmium::OSMObject const &object)
{
    return check_input(last, {object.type(), object.id(), object.version()});
}

/**
 * A data source is where we get the OSM objects from, one at a time. It
 * wraps the osmium::io::Reader.
 */
class data_source_t
{
public:
    explicit data_source_t(osmium::io::File const &file)
    : m_reader(new osmium::io::Reader{file})
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
    type_id_version m_last = {osmium::item_type::node, 0, 0};

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

static osmium::item_type apply(osmium::OSMObject &object, osmdata_t &osmdata,
                               progress_display_t &progress)
{
    static osmium::item_type last_type = osmium::item_type::node;

    if (last_type != object.type()) {
        if (last_type == osmium::item_type::node) {
            osmdata.after_nodes();
            progress.start_way_counter();
        }
        if (object.type() == osmium::item_type::relation) {
            osmdata.after_ways();
            progress.start_relation_counter();
        }
        last_type = object.type();
    }

    osmium::apply_item(object, osmdata, progress);

    return last_type;
}

static osmium::item_type process_single_file(osmium::io::File const &file,
                                             osmdata_t &osmdata,
                                             progress_display_t &progress,
                                             bool append)
{
    osmium::io::Reader reader{file};
    type_id_version last{osmium::item_type::node, 0, 0};

    osmium::item_type last_type = osmium::item_type::node;
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (auto &object : buffer.select<osmium::OSMObject>()) {
            last = check_input(last, object);
            if (!append && object.deleted()) {
                throw std::runtime_error{
                    "Input file contains deleted objects but "
                    "you are not in append mode."};
            }
            last_type = apply(object, osmdata, progress);
        }
    }

    reader.close();

    return last_type;
}

static osmium::item_type
process_multiple_files(std::vector<osmium::io::File> const &files,
                       osmdata_t &osmdata, progress_display_t &progress,
                       bool append)
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

    osmium::item_type last_type = osmium::item_type::node;
    while (!queue.empty()) {
        auto element = queue.top();
        queue.pop();
        if (queue.empty() || element != queue.top()) {
            if (!append && element.object().deleted()) {
                throw std::runtime_error{
                    "Input file contains deleted objects but "
                    "you are not in append mode."};
            }
            last_type = apply(element.object(), osmdata, progress);
        }

        auto *source = element.data_source();
        if (source->next()) {
            queue.emplace(source->get(), source);
        }
    }

    for (auto &data_source : data_sources) {
        data_source.close();
    }

    return last_type;
}

void process_files(std::vector<osmium::io::File> const &files,
                   osmdata_t &osmdata, bool append, bool show_progress)
{
    progress_display_t progress{show_progress};

    auto const last_type =
        (files.size() == 1)
            ? process_single_file(files.front(), osmdata, progress, append)
            : process_multiple_files(files, osmdata, progress, append);

    switch (last_type) {
        case osmium::item_type::node:
            osmdata.after_nodes();
            // fallthrough
        case osmium::item_type::way:
            osmdata.after_ways();
            break;
        default:
            break;
    }

    osmdata.after_relations();
    progress.print_summary();
}
