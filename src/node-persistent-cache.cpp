#define _LARGEFILE64_SOURCE /* See feature_test_macros(7) */

#include "logging.hpp"
#include "node-persistent-cache.hpp"
#include "options.hpp"

void node_persistent_cache::set(osmid_t id, osmium::Location location)
{
    if (id < 0) {
        throw std::runtime_error{"Flatnode store cannot save negative IDs."};
    }
    m_index->set(static_cast<osmium::unsigned_object_id_type>(id), location);
}

osmium::Location node_persistent_cache::get(osmid_t id) const noexcept
{
    if (id < 0) {
        return osmium::Location{};
    }

    return m_index->get_noexcept(
        static_cast<osmium::unsigned_object_id_type>(id));
}

std::size_t node_persistent_cache::get_list(osmium::WayNodeList *nodes) const
{
    std::size_t count = 0;

    for (auto &n : *nodes) {
        auto loc = m_ram_cache->get(n.ref());
        if (!loc.valid() && n.ref() >= 0) {
            loc = m_index->get_noexcept(
                static_cast<osmium::unsigned_object_id_type>(n.ref()));
        }
        n.set_location(loc);
        if (loc.valid()) {
            ++count;
        }
    }

    return count;
}

node_persistent_cache::node_persistent_cache(
    const options_t *options, std::shared_ptr<node_ram_cache> ptr)
: m_ram_cache(std::move(ptr))
{
    if (!options->flat_node_file) {
        throw std::runtime_error{"Unable to set up persistent cache: the name "
                                 "of the flat node file was not set."};
    }

    m_fname = options->flat_node_file->c_str();
    m_remove_file = options->droptemp;
    log_info("Mid: loading persistent node cache from {}", m_fname);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-signed-bitwise)
    m_fd = open(m_fname, O_RDWR | O_CREAT, 0644);
    if (m_fd < 0) {
        log_error("Cannot open location cache file '{}': {}", m_fname,
                  std::strerror(errno));
        throw std::runtime_error{"Unable to open flatnode file."};
    }

    m_index.reset(new index_t{m_fd});
}

node_persistent_cache::~node_persistent_cache() noexcept
{
    m_index.reset();
    if (m_fd >= 0) {
        close(m_fd);
    }

    if (m_remove_file) {
        try {
            log_info("Mid: removing persistent node cache at {}", m_fname);
        } catch (...) {
        }
        unlink(m_fname);
    }
}
