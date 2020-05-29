#ifndef OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP
#define OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP

#include <memory>

#include <osmium/index/map/dense_file_array.hpp>
#include <osmium/osm/location.hpp>

#include "node-ram-cache.hpp"
#include "osmtypes.hpp"

class options_t;

class node_persistent_cache
{
public:
    node_persistent_cache(options_t const *options,
                          std::shared_ptr<node_ram_cache> ptr);
    ~node_persistent_cache() noexcept;

    void set(osmid_t id, osmium::Location location);
    osmium::Location get(osmid_t id) const noexcept;
    std::size_t get_list(osmium::WayNodeList *nodes) const;

private:
    // Dense node cache for unsigned IDs only
    using index_t =
        osmium::index::map::DenseFileArray<osmium::unsigned_object_id_type,
                                           osmium::Location>;

    std::shared_ptr<node_ram_cache> m_ram_cache;
    int m_fd = -1;
    std::unique_ptr<index_t> m_index;
    bool m_remove_file = false;
    char const *m_fname = nullptr;
};

#endif // OSM2PGSQL_NODE_PERSISTENT_CACHE_HPP
