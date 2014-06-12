#ifndef PROCESSOR_POLYGON_HPP
#define PROCESSOR_POLYGON_HPP

#include "geometry-processor.hpp"

struct processor_polygon : public geometry_processor {
    processor_polygon(int srid, bool enable_multi);
    virtual ~processor_polygon();

    geometry_builder::maybe_wkt_t process_way(const osmid_t *node_ids, size_t const node_count, const middle_query_t *mid);
    geometry_builder::maybe_wkts_t process_relation(const osmNode * const * nodes, const int* node_counts, const middle_query_t *mid);

private:
    bool enable_multi;
    geometry_builder builder;
    std::vector<osmNode> node_cache;
};

#endif /* PROCESSOR_POLYGON_HPP */
