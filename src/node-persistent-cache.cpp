#define _LARGEFILE64_SOURCE /* See feature_test_macros(7) */

#include "logging.hpp"
#include "node-persistent-cache.hpp"

#include <cassert>

void node_persistent_cache::set(osmid_t id, osmium::Location location)
{
    m_index->set(static_cast<osmium::unsigned_object_id_type>(id), location);
}

osmium::Location node_persistent_cache::get(osmid_t id) const noexcept
{
    return m_index->get_noexcept(
        static_cast<osmium::unsigned_object_id_type>(id));
}

node_persistent_cache::node_persistent_cache(std::string const &file_name, bool remove_file)
{
    assert(!file_name.empty());

    m_fname = file_name.c_str();
    m_remove_file = remove_file;
    log_debug("Mid: loading persistent node cache from {}", m_fname);

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
            log_debug("Mid: removing persistent node cache at {}", m_fname);
        } catch (...) {
        }
        unlink(m_fname);
    }
}
