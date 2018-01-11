#ifndef NODE_PERSISTENT_CACHE_H
#define NODE_PERSISTENT_CACHE_H

#include <memory>

#include <osmium/index/map/dense_file_array.hpp>
#include <osmium/osm/location.hpp>

#include "node-ram-cache.hpp"
#include "osmtypes.hpp"

struct options_t;
class reprojection;

class node_persistent_cache
{
public:
    node_persistent_cache(options_t const *options,
                          std::shared_ptr<node_ram_cache> ptr);
    ~node_persistent_cache();

    void set(osmid_t id, osmium::Location const &coord);
    osmium::Location get(osmid_t id);
    size_t get_list(osmium::WayNodeList *nodes);

private:
    // Dense node cache for unsigned IDs only
    using index_t =
        osmium::index::map::DenseFileArray<osmium::unsigned_object_id_type,
                                           osmium::Location>;

    std::shared_ptr<node_ram_cache> m_ram_cache;
    int m_fd;
    std::unique_ptr<index_t> m_index;
    bool m_remove_file;
    const char *m_fname;
};

#endif
