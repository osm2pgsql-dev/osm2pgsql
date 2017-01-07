#define _LARGEFILE64_SOURCE     /* See feature_test_macrors(7) */

#include "node-persistent-cache.hpp"
#include "options.hpp"
#include "reprojection.hpp"

void node_persistent_cache::set(osmid_t id, const osmium::Location &coord)
{
    if (id < 0) {
        throw std::runtime_error("Flatnode store cannot save negative IDs.");
    }
    m_index->set(static_cast<osmium::unsigned_object_id_type>(id), coord);
}

osmium::Location node_persistent_cache::get(osmid_t id)
{
    if (id >= 0) {
        try {
            return m_index->get(
                static_cast<osmium::unsigned_object_id_type>(id));
        } catch (osmium::not_found const &) {
        }
    }

    return osmium::Location();
}

size_t node_persistent_cache::get_list(nodelist_t &out,
                                       osmium::WayNodeList const &nds,
                                       reprojection const *proj)
{
    assert(nds.empty());
    out.reserve(nds.size());

    for (auto const &n : nds) {
        /* Check cache first */
        auto loc = m_ram_cache->get(n.ref());
        if (!loc.valid()) {
            loc = get(n.ref());
        }
        if (loc.valid()) {
            auto coord = proj->reproject(loc);
            out.emplace_back(coord.x, coord.y);
        }
    }

    return out.size();
}

node_persistent_cache::node_persistent_cache(
    const options_t *options, std::shared_ptr<node_ram_cache> ptr)
: m_ram_cache(ptr), m_fd(-1)
{
    if (!options->flat_node_file) {
        throw std::runtime_error("Unable to set up persistent cache: the name "
                                 "of the flat node file was not set.");
    }

    auto fname = options->flat_node_file->c_str();
    fprintf(stderr, "Mid: loading persistent node cache from %s\n", fname);

    m_fd = open(fname, O_RDWR | O_CREAT, 0644);
    if (m_fd < 0) {
        fprintf(stderr, "Cannot open location cache file '%s': %s\n", fname,
                std::strerror(errno));
        throw std::runtime_error("Unable to open flatnode file\n");
    }

    m_index.reset(new index_t{m_fd});
}

node_persistent_cache::~node_persistent_cache()
{
    m_index.reset();
    if (m_fd >= 0) {
        close(m_fd);
    }
}
