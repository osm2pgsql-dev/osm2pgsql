#include "output-multi.hpp"

output_multi_t::output_multi_t(const middle_query_t* mid_, const options_t* options_) 
    : output_t(mid_, options_),
      m_tagtransform(NULL),
      m_table(),
      m_export_list(NULL),
      m_sql(),
      m_processor(),
      m_geo_interest(m_processor->interests()) {
}

output_multi_t::~output_multi_t() {
}

int output_multi_t::start() {
    // TODO!
    return 0;
}

middle_t::way_cb_func *output_multi_t::way_callback() {
    // TODO!
    return NULL;
}

middle_t::rel_cb_func *output_multi_t::relation_callback() {
    // TODO!
    return NULL;
}

void output_multi_t::stop() {
    // TODO!
}

void output_multi_t::commit() {
    // TODO!
}

void output_multi_t::cleanup() {
    // TODO!
}

int output_multi_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_node) > 0) {
        return process_node(id, lat, lon, tags);
    }
    return 0;
}

int output_multi_t::way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_way) > 0) {
        return process_way(id, nodes, node_count, tags);
    }
    return 0;
}


int output_multi_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_relation) > 0) {
        return process_relation(id, members, member_count, tags);
    }
    return 0;
}

int output_multi_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_node) > 0) {
        // TODO - need to know it's a node?
        delete_from_output(id);

        // TODO: need to mark any ways or relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_node(id, lat, lon, tags);

    } else {
        return 0;
    }
}

int output_multi_t::way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_way) > 0) {
        // TODO - need to know it's a way?
        delete_from_output(id);

        // TODO: need to mark any relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_way(id, nodes, node_count, tags);

    } else {
        return 0;
    }
}

int output_multi_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    if ((m_geo_interest & geometry_processor::interest_relation) > 0) {
        // TODO - need to know it's a relation?
        delete_from_output(id);

        // TODO: need to mark any other relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_relation(id, members, member_count, tags);

    } else {
        return 0;
    }
}

int output_multi_t::node_delete(osmid_t id) {
    if ((m_geo_interest & geometry_processor::interest_node) > 0) {
        // TODO - need to know it's a node?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::way_delete(osmid_t id) {
    if ((m_geo_interest & geometry_processor::interest_way) > 0) {
        // TODO - need to know it's a way?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::relation_delete(osmid_t id) {
    if ((m_geo_interest & geometry_processor::interest_relation) > 0) {
        // TODO - need to know it's a relation?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::process_node(osmid_t id, double lat, double lon, struct keyval *tags) {
    unsigned int filter = m_tagtransform->filter_node_tags(tags, m_export_list);
    if (!filter) {
        geometry_processor::maybe_wkt_t wkt = m_processor->process_node(lat, lon);
        if (wkt) {
            copy_to_table(id, wkt->c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list);
    if (!filter) {
        geometry_processor::maybe_wkt_t wkt = m_processor->process_way(nodes, node_count, m_mid);
        if (wkt) {
            copy_to_table(id, wkt->c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    unsigned int filter = m_tagtransform->filter_rel_tags(tags, m_export_list);
    if (!filter) {
        geometry_processor::maybe_wkt_t wkt = m_processor->process_relation(members, member_count, m_mid);
        if (wkt) {
            copy_to_table(id, wkt->c_str(), tags);
        }
    }
    return 0;
}

void copy_to_table(osmid_t id, const char *wkt, const struct keyval *tags) {
    // TODO: implement me!
}
