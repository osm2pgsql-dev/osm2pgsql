#ifndef PROCESSOR_LINE_HPP
#define PROCESSOR_LINE_HPP

#include "geometry-processor.hpp"

struct processor_line : public geometry_processor {
    processor_line(int srid);
    virtual ~processor_line();

    geometry_builder::maybe_wkt_t process_way(const osmid_t *node_ids, size_t node_count, const middle_query_t *mid);

private:
    geometry_builder builder;
    std::vector<osmNode> node_cache;
};

#endif /* PROCESSOR_LINE_HPP */
