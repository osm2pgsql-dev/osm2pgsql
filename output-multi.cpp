#include "output-multi.hpp"
#include "taginfo_impl.hpp"

namespace {

columns_t normal_columns(const export_list &cols) {
    // TODO: shouldn't need type here.
    return cols.normal_columns(OSMTYPE_WAY);
}

} // anonymous namespace

output_multi_t::output_multi_t(const std::string &name,
                               boost::shared_ptr<geometry_processor> processor_,
                               struct export_list *export_list_,
                               const middle_query_t* mid_, const options_t &options_) 
    : output_t(mid_, options_),
      m_tagtransform(new tagtransform(&m_options)),
      m_export_list(new export_list(*export_list_)),
      m_sql(),
      m_processor(processor_),
      m_geo_interest(m_processor->interests()),
      m_table(new table_t(name, m_processor->type(), normal_columns(*m_export_list),
                          m_options.hstore_columns, m_processor->srid(), m_options.scale,
                          m_options.append, m_options.slim, m_options.droptemp,
                          m_options.hstore_mode, m_options.enable_hstore_index,
                          m_options.tblsmain_data, m_options.tblsmain_index)) {
}

output_multi_t::~output_multi_t() {
}

int output_multi_t::start() {
    // TODO: id tracker?
    m_table->setup(m_options.conninfo);
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
    m_table->pgsql_pause_copy();
    // TODO: do some stuff here similar to output_pgsql_t::pgsql_out_stop_one
    cleanup();
}

void output_multi_t::commit() {
    m_table->commit();
    // TODO: any id trackers too
}

void output_multi_t::cleanup() {
    m_table->teardown();
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
    unsigned int filter = m_tagtransform->filter_node_tags(tags, m_export_list.get());
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
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get());
    if (!filter) {
        geometry_processor::maybe_wkt_t wkt = m_processor->process_way(nodes, node_count, m_mid);
        if (wkt) {
            copy_to_table(id, wkt->c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    unsigned int filter = m_tagtransform->filter_rel_tags(tags, m_export_list.get());
    if (!filter) {
        geometry_processor::maybe_wkt_t wkt = m_processor->process_relation(members, member_count, m_mid);
        if (wkt) {
            copy_to_table(id, wkt->c_str(), tags);
        }
    }
    return 0;
}

void output_multi_t::copy_to_table(osmid_t id, const char *wkt, struct keyval *tags) {
    m_table->write_wkt(id, tags, wkt, m_sql);
}

void output_multi_t::delete_from_output(osmid_t id) {
    m_table->delete_row(id);
}

