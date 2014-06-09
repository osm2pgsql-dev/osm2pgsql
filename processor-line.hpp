#ifndef PROCESSOR_LINE_HPP
#define PROCESSOR_LINE_HPP

#include "geometry-processor.hpp"

struct processor_line : public geometry_processor {
    processor_line(int srid);
    virtual ~processor_line();

    geometry_builder::maybe_wkt_t process_way(osmid_t *node_ids, size_t node_count, const middle_query_t *mid);
    geometry_builder::maybe_wkts_t process_relation(member *members, size_t member_count, const middle_query_t *mid);

private:
    geometry_builder builder;
};

#endif /* PROCESSOR_LINE_HPP */
